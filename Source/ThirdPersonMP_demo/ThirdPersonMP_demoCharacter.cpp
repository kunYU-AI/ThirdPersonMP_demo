// Copyright Epic Games, Inc. All Rights Reserved.

#include "ThirdPersonMP_demoCharacter.h"
#include "Engine/LocalPlayer.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/Controller.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputActionValue.h"
#include "TPMPWeapon.h"
#include "TPMPRifle.h"
#include "Net/UnrealNetwork.h"
#include "Engine/Engine.h"

DEFINE_LOG_CATEGORY(LogTemplateCharacter);

//////////////////////////////////////////////////////////////////////////
// AThirdPersonMP_demoCharacter

AThirdPersonMP_demoCharacter::AThirdPersonMP_demoCharacter()
{
	// Set size for collision capsule
	GetCapsuleComponent()->InitCapsuleSize(42.f, 96.0f);
		
	// Don't rotate when the controller rotates. Let that just affect the camera.
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	// Configure character movement
	GetCharacterMovement()->bOrientRotationToMovement = true; // Character moves in the direction of input...	
	GetCharacterMovement()->RotationRate = FRotator(0.0f, 500.0f, 0.0f); // ...at this rotation rate

	// Note: For faster iteration times these variables, and many more, can be tweaked in the Character Blueprint
	// instead of recompiling to adjust them
	GetCharacterMovement()->JumpZVelocity = 700.f;
	GetCharacterMovement()->AirControl = 0.35f;
	GetCharacterMovement()->MaxWalkSpeed = 500.f;
	GetCharacterMovement()->MinAnalogWalkSpeed = 20.f;
	GetCharacterMovement()->BrakingDecelerationWalking = 2000.f;
	GetCharacterMovement()->BrakingDecelerationFalling = 1500.0f;

	// Create a camera boom (pulls in towards the player if there is a collision)
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength = 400.0f; // The camera follows at this distance behind the character	
	CameraBoom->bUsePawnControlRotation = true; // Rotate the arm based on the controller

	// Create a follow camera
	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName); // Attach the camera to the end of the boom and let the boom adjust to match the controller orientation
	FollowCamera->bUsePawnControlRotation = false; // Camera does not rotate relative to arm

	// Note: The skeletal mesh and anim blueprint references on the Mesh component (inherited from Character) 
	// are set in the derived blueprint asset named ThirdPersonCharacter (to avoid direct content references in C++)

	bReplicates = true;
	SetReplicateMovement(true);

	// 碰撞设置
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	if (USkeletalMeshComponent* MeshComp = GetMesh())
	{
		MeshComp->SetCollisionObjectType(ECC_Pawn);
		MeshComp->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	}

	GetCharacterMovement()->NetworkSmoothingMode = ENetworkSmoothingMode::Exponential;

	MaxHealth = 100.0f;
	CurrentHealth = MaxHealth;
	bIsDead = false;
}

void AThirdPersonMP_demoCharacter::BeginPlay()
{
	// Call the base class  
	Super::BeginPlay();

	// 服务器：生成默认武器并装备
	if (HasAuthority() && DefaultWeaponClass)
	{
		Server_SpawnAndEquipDefaultWeapon();
	}
}

void AThirdPersonMP_demoCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// 本地玩家连射期间
	if (IsLocallyControlled() && bIsFiring && CurrentWeapon)
	{
		CurrentWeapon->Server_UpdateAimPoint(ComputeAimData());
	}

	// 持枪时，调整角色面朝摄像机方向
	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		const bool bCombatFacing = !bIsDead && (CurrentWeapon != nullptr);
		if (bCombatFacing)
		{
			MoveComp->bOrientRotationToMovement = false;
			bUseControllerRotationYaw = false;
			if (Controller)
			{
				const FRotator TargetRot(0.f, Controller->GetControlRotation().Yaw, 0.f);
				const FRotator CurRot(0.f, GetActorRotation().Yaw, 0.f);
				const float YawDelta = FMath::Abs(FRotator::NormalizeAxis(TargetRot.Yaw - CurRot.Yaw));

				if (bIsFiring)
				{
					// 开火期间：快速对齐
					SetActorRotation(FMath::RInterpTo(CurRot, TargetRot, DeltaSeconds, 20.0f));
				}
				else if (YawDelta >= 60.f)
				{
					// 大角度偏差：角色身体跟转
					SetActorRotation(FMath::RInterpTo(CurRot, TargetRot, DeltaSeconds, 5.0f));
				}
				else if (GetVelocity().Size2D() > 50.f)
				{
					// 移动中：角色也需跟转
					SetActorRotation(FMath::RInterpTo(CurRot, TargetRot, DeltaSeconds, 8.0f));
				}
			}
		}
		else
		{
			MoveComp->bOrientRotationToMovement = !bIsDead;
			bUseControllerRotationYaw = false;
		}
	}
}

//////////////////////////////////////////////////////////////////////////
// Input

void AThirdPersonMP_demoCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	// Add Input Mapping Context
	if (APlayerController* PlayerController = Cast<APlayerController>(GetController()))
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PlayerController->GetLocalPlayer()))
		{
			Subsystem->AddMappingContext(DefaultMappingContext, 0);
		}
	}
	
	// Set up action bindings
	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent)) {
		
		// Jumping
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Started, this, &ACharacter::Jump);
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Completed, this, &ACharacter::StopJumping);

		// Moving
		EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AThirdPersonMP_demoCharacter::Move);

		// Looking
		EnhancedInputComponent->BindAction(LookAction, ETriggerEvent::Triggered, this, &AThirdPersonMP_demoCharacter::Look);

		// Fire
		EnhancedInputComponent->BindAction(FireAction, ETriggerEvent::Started, this, &AThirdPersonMP_demoCharacter::StartFire);
		EnhancedInputComponent->BindAction(FireAction, ETriggerEvent::Completed, this, &AThirdPersonMP_demoCharacter::StopFire);
	}
	else
	{
		UE_LOG(LogTemplateCharacter, Error, TEXT("'%s' Failed to find an Enhanced Input component! This template is built to use the Enhanced Input system. If you intend to use the legacy system, then you will need to update this C++ file."), *GetNameSafe(this));
	}
}

void AThirdPersonMP_demoCharacter::Move(const FInputActionValue& Value)
{
	if (bIsDead)
	{
		return;
	}

	// input is a Vector2D
	FVector2D MovementVector = Value.Get<FVector2D>();

	if (Controller != nullptr)
	{
		// find out which way is forward
		const FRotator Rotation = Controller->GetControlRotation();
		const FRotator YawRotation(0, Rotation.Yaw, 0);

		// get forward vector
		const FVector ForwardDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
	
		// get right vector 
		const FVector RightDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

		// add movement 
		AddMovementInput(ForwardDirection, MovementVector.Y);
		AddMovementInput(RightDirection, MovementVector.X);
	}
}

void AThirdPersonMP_demoCharacter::Look(const FInputActionValue& Value)
{
	if (bIsDead)
	{
		return;
	}

	// input is a Vector2D
	FVector2D LookAxisVector = Value.Get<FVector2D>();

	if (Controller != nullptr)
	{
		// add yaw and pitch input to controller
		AddControllerYawInput(LookAxisVector.X);
		AddControllerPitchInput(LookAxisVector.Y);
	}
}

void AThirdPersonMP_demoCharacter::StartFire()
{
	if (bIsDead || !CurrentWeapon)
	{
		return;
	}

	bIsFiring = true;
	CurrentWeapon->Server_StartFire(ComputeAimData());
}


void AThirdPersonMP_demoCharacter::StopFire()
{
	bIsFiring = false;

	if (!CurrentWeapon) return;
	CurrentWeapon->Server_StopFire();
}


// 装备武器
void AThirdPersonMP_demoCharacter::Server_SpawnAndEquipDefaultWeapon_Implementation()
{
	if (!HasAuthority() || !DefaultWeaponClass || CurrentWeapon) return;

	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.Instigator = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	ATPMPRifle* NewWeapon = GetWorld()->SpawnActor<ATPMPRifle>(DefaultWeaponClass, GetActorLocation(), GetActorRotation(), Params);
	if (NewWeapon)
	{
		NewWeapon->EquipTo(this);
		CurrentWeapon = NewWeapon;
	}
}

void AThirdPersonMP_demoCharacter::OnRep_CurrentWeapon()
{
	// 客户端：武器复制后已自动 attach（Actor attachment 经 NetCore 复制）；这里可触发 UI 更新
}

void AThirdPersonMP_demoCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AThirdPersonMP_demoCharacter, CurrentHealth);
	DOREPLIFETIME(AThirdPersonMP_demoCharacter, bIsDead);
	DOREPLIFETIME(AThirdPersonMP_demoCharacter, CurrentWeapon);
}


FTPMPAimData AThirdPersonMP_demoCharacter::ComputeAimData() const
{
	FVector CamLoc;
	FRotator CamRot;
	if (const APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		PC->GetPlayerViewPoint(CamLoc, CamRot);
	}
	else if (FollowCamera)
	{
		CamLoc = FollowCamera->GetComponentLocation();
		CamRot = FollowCamera->GetComponentRotation();
	}
	else
	{
		CamLoc = GetActorLocation();
		CamRot = GetActorRotation();
	}

	// 准星射线：从摄像机沿视线打出，命中点即屏幕中心准星真正指向的世界位置。
	const FVector TraceStart = CamLoc;
	const FVector TraceEnd = CamLoc + CamRot.Vector() * 50000.0f;

	FCollisionQueryParams Params;
	Params.AddIgnoredActor(this);
	if (CurrentWeapon)
	{
		Params.AddIgnoredActor(CurrentWeapon);
	}

	FTPMPAimData AimData;
	AimData.Origin = TraceStart;

	// 忽略离摄像机过近的命中，避免俯视时准星点落在脚边、子弹扎地
	const float MinValidDist = 150.0f;
	TArray<FHitResult> Hits;
	if (GetWorld()->LineTraceMultiByChannel(Hits, TraceStart, TraceEnd, ECC_Visibility, Params))
	{
		for (const FHitResult& H : Hits)
		{
			if (H.Distance >= MinValidDist)
			{
				AimData.ImpactPoint = H.ImpactPoint;
				return AimData;
			}
		}
	}
	// 未命中任何有效物体：返回视线远点
	AimData.ImpactPoint = TraceEnd;
	return AimData;
}

// 回调复制后
void AThirdPersonMP_demoCharacter::OnRep_CurrentHealth()
{
	OnHealthUpdate();
}

void AThirdPersonMP_demoCharacter::OnHealthUpdate()
{
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(
			1,                                  // Key：固定为1，新消息覆盖旧的
			5.0f,                               // 显示时长（秒）
			FColor::Green,                      // 颜色
			FString::Printf(TEXT("HP: %.1f / %.1f  Dead=%s"),
				CurrentHealth, MaxHealth,
				bIsDead ? TEXT("YES") : TEXT("NO"))
		);
	}
}

void AThirdPersonMP_demoCharacter::OnRep_IsDead()
{
	// 死亡表现统一由 Multicast_PlayDeath 驱动（一次性事件用 Multicast 比 OnRep 可靠）
}

void AThirdPersonMP_demoCharacter::SetCurrentHealth(float HealthValue)
{
	if (HasAuthority())
	{
		CurrentHealth = FMath::Clamp(HealthValue, 0.f, MaxHealth);
		OnHealthUpdate();
		if (CurrentHealth <= 0.f && !bIsDead)
		{
			bIsDead = true;
			HandleDeath();
		}
	}
}

float AThirdPersonMP_demoCharacter::TakeDamage(float DamageTaken, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	if (bIsDead) return 0.f;

	const float Applied = Super::TakeDamage(DamageTaken, DamageEvent, EventInstigator, DamageCauser);

	if (HasAuthority())
	{
		SetCurrentHealth(CurrentHealth - DamageTaken);
	}

	return Applied;
}

void AThirdPersonMP_demoCharacter::HandleDeath()
{
	// 仅服务器：处理权威状态 + 广播死亡表现
	if (!HasAuthority())
	{
		return;
	}

	bIsFiring = false;

	// 关闭武器开火
	if (CurrentWeapon)
	{
		CurrentWeapon->Server_StopFire();
	}

	// 广播死亡表现给所有端（含 server 本地）。死亡动画是一次性事件，用 Multicast 比 OnRep 可靠。
	Multicast_PlayDeath();
}


void AThirdPersonMP_demoCharacter::Multicast_PlayDeath_Implementation()
{
	PlayDeathVisuals();
}


void AThirdPersonMP_demoCharacter::PlayDeathVisuals()
{
	bIsFiring = false;

	// 禁用输入
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		DisableInput(PC);
	}

	// 直接 Ragdoll 瘫地
	StartRagdoll();

	// 关闭碰撞胶囊
	if (UCapsuleComponent* Cap = GetCapsuleComponent())
	{
		Cap->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	// 停止移动
	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		MoveComp->StopMovementImmediately();
		MoveComp->DisableMovement();
	}
}


void AThirdPersonMP_demoCharacter::StartRagdoll()
{
	USkeletalMeshComponent* MeshComp = GetMesh();
	if (!MeshComp || MeshComp->IsSimulatingPhysics())
	{
		return;
	}

	// 让骨骼网格进入布娃娃物理模拟
	MeshComp->SetCollisionProfileName(TEXT("Ragdoll"));
	MeshComp->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	MeshComp->SetAllBodiesSimulatePhysics(true);
	MeshComp->SetSimulatePhysics(true);
	MeshComp->WakeAllRigidBodies();
}

