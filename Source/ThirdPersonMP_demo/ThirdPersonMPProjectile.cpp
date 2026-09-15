// Fill out your copyright notice in the Description page of Project Settings.


#include "ThirdPersonMPProjectile.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "GameFramework/DamageType.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraDataInterfaceArrayFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Sound/SoundBase.h"
#include "Kismet/GameplayStatics.h"


// Sets default values
AThirdPersonMPProjectile::AThirdPersonMPProjectile()
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	SetReplicateMovement(true);
	// 如果不强制相关，则远离角色一定距离/视野后，他就会消失，而看不见命中特效
	bAlwaysRelevant = true;

	// 球形组件：碰撞
	SphereComponent = CreateDefaultSubobject<USphereComponent>(TEXT("SphereComponent"));
	SphereComponent->InitSphereRadius(8.0f);
	SphereComponent->SetCollisionProfileName(TEXT("BlockAllDynamic"));  // BlockAll+Dynamic：设置自己的碰撞预设，是一个阻挡所有物体且动态的碰撞物
	SphereComponent->SetNotifyRigidBodyCollision(true);  // 打开碰撞的事件通知开关，可以广播 OnComponentHit 来回调
	SphereComponent->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);

	RootComponent = SphereComponent;

	// 静态网格体
	StaticMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("StaticMesh"));
	StaticMesh->SetupAttachment(RootComponent);
	StaticMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// Projectile Movement
	ProjectileMovementComponent = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("ProjectileMovement"));
	ProjectileMovementComponent->SetUpdatedComponent(SphereComponent);
	ProjectileMovementComponent->InitialSpeed = 8000.0f;
	ProjectileMovementComponent->MaxSpeed = 8000.0f;               // 匀速
	ProjectileMovementComponent->bRotationFollowsVelocity = true;  // 旋转跟随速度，避免横着飞
	ProjectileMovementComponent->ProjectileGravityScale = 0.0f;

	// 拖尾特效组件
	TracerComponent = CreateDefaultSubobject<UNiagaraComponent>(TEXT("TracerComponent"));
	TracerComponent->SetupAttachment(RootComponent);
	TracerComponent->SetAutoActivate(true);  // 拖尾特效在子弹actor生成时激活

	DamageType = UDamageType::StaticClass();
	Damage = 15.0f;
	InitialSpeed = 8000.0f;
	MaxSpeed = 8000.0f;
	ProjectileLifetime = 3.0f;
}

// Called when the game starts or when spawned
void AThirdPersonMPProjectile::BeginPlay()
{
	Super::BeginPlay();

	// 1. 同步蓝图中编辑完的属性
	if (ProjectileMovementComponent)
	{
		ProjectileMovementComponent->InitialSpeed = InitialSpeed;
		ProjectileMovementComponent->MaxSpeed = MaxSpeed;
	}

	// 2.1 SphereComponent设置避免碰撞角色
	if (AActor* ProjOwner = GetOwner())  // 角色的控制器
	{
		if (SphereComponent)
		{
			SphereComponent->IgnoreActorWhenMoving(ProjOwner, true);
		}
	}
	if (APawn* InstigatorPawn = GetInstigator())
	{
		if (SphereComponent && InstigatorPawn != GetOwner())
		{
			SphereComponent->IgnoreActorWhenMoving(InstigatorPawn, true);
		}
	}

	// 2.2 服务器才注册碰撞回调
	if (HasAuthority() && SphereComponent)
	{
		SphereComponent->OnComponentHit.AddDynamic(this, &AThirdPersonMPProjectile::OnProjectileImpact);
	}

	// 2.3 销毁
	if (HasAuthority())
	{
		SetLifeSpan(ProjectileLifetime);
	}

	// 3. 启动Tracer
	if (TracerComponent && TracerFX)
	{
		TracerComponent->SetAsset(TracerFX);
		TracerComponent->Activate(true);
		TracerComponent->SetVariableBool(TEXT("Trigger"), true);
	}
}

// Called every frame
void AThirdPersonMPProjectile::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

void AThirdPersonMPProjectile::Destroyed()
{
	Super::Destroyed();
}

void AThirdPersonMPProjectile::OnProjectileImpact(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
	if (!HasAuthority()) return;

	if (OtherActor && (OtherActor == GetOwner() || OtherActor == GetInstigator()))
	{
		return;
	}

	// 多播特效
	Multicast_PlayImpactFX(Hit.ImpactPoint, Hit.ImpactNormal);

	// 命中后的逻辑：停止移动 + 隐藏 + 消失
	if (ProjectileMovementComponent)
	{
		ProjectileMovementComponent->StopMovementImmediately();
	}
	if (StaticMesh)
	{
		StaticMesh->SetVisibility(false);
	}
	SetLifeSpan(0.2f);
}

void AThirdPersonMPProjectile::Multicast_PlayImpactFX_Implementation(const FVector_NetQuantize& ImpactLoc, const FVector_NetQuantizeNormal& ImpactNormal)
{
	UWorld* World = GetWorld();
	if (!World) return;

	const FRotator ImpactRot = ImpactNormal.Rotation();  // ImpactNormal是碰撞面的法向量，ImpactRot是让一个物体朝向这个方向，需要进行多少旋转

	if (ImpactFX)
	{
		UNiagaraComponent* SpawnedFX = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			World, ImpactFX, ImpactLoc, ImpactRot,
			FVector(1.0f),
			true,   // bAutoDestroy
			false,  // bAutoActivate = false：先别播，等设完参数再激活
			ENCPoolMethod::None,
			true    // bPreCullCheck
		);

		if (SpawnedFX)
		{
			TArray<FVector> Positions; 
			Positions.Add(FVector(ImpactLoc));
			TArray<FVector> Normals;   
			Normals.Add(FVector(ImpactNormal));

			UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayPosition(
				SpawnedFX, TEXT("ImpactPositions"), Positions);

			UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayVector(
				SpawnedFX, TEXT("ImpactNormals"), Normals);

			SpawnedFX->SetVariablePosition(TEXT("MuzzlePosition"), ImpactLoc);
			SpawnedFX->SetIntParameter(TEXT("NumberOfHits"), 1);

			SpawnedFX->Activate(true);
		}
	}

	if (ImpactSound)
	{
		UGameplayStatics::PlaySoundAtLocation(World, ImpactSound, ImpactLoc);
	}
}