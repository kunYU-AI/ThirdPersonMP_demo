// Fill out your copyright notice in the Description page of Project Settings.


#include "TPMPWeapon.h"
#include "ThirdPersonMP_demoCharacter.h"
#include "ThirdPersonMPProjectile.h"
#include "Components/SkeletalMeshComponent.h"
#include "Net/UnrealNetwork.h"
#include "GameFramework/DamageType.h"
#include "Kismet/GameplayStatics.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimInstance.h"

// Sets default values
ATPMPWeapon::ATPMPWeapon()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false; // 扣扳机时才启用

	bReplicates = true;
	SetReplicateMovement(false);

	WeaponMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("WeaponMesh"));
	WeaponMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	WeaponMesh->SetSimulatePhysics(false);
	RootComponent = WeaponMesh;

	MuzzleSocketName = TEXT("MuzzleFlash");
	CharacterAttachSocketName = TEXT("hand_r_socket");
	FireRate = 0.1f;
	Damage = 1.0f;
	bIsAutomatic = true;

	bIsTriggerHeld = false;
	LastFireTime = -1000.0f;
	OwnerCharacter = nullptr;
}

// Called when the game starts or when spawned
void ATPMPWeapon::BeginPlay()
{
	Super::BeginPlay();
	
}

// Called every frame
void ATPMPWeapon::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (HasAuthority() && bIsTriggerHeld && bIsAutomatic)
	{
		const float Now = GetWorld()->GetTimeSeconds();
		if (Now - LastFireTime >= FireRate)
		{
			HandleFire_Server();
		}
	}
}

void ATPMPWeapon::EquipTo(AThirdPersonMP_demoCharacter* NewOwner)
{
	if (!NewOwner) return;

	OwnerCharacter = NewOwner;
	SetOwner(NewOwner);
	SetInstigator(NewOwner);

	if (USkeletalMeshComponent* CharacterMesh = NewOwner->GetMesh())
	{
		AttachToComponent(
			CharacterMesh,
			FAttachmentTransformRules(EAttachmentRule::SnapToTarget, true),
			CharacterAttachSocketName
		);
	}
}

void ATPMPWeapon::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATPMPWeapon, bIsTriggerHeld);
	DOREPLIFETIME(ATPMPWeapon, OwnerCharacter);
}

void ATPMPWeapon::Server_StartFire_Implementation(const FTPMPAimData& AimData)
{
	if (!OwnerCharacter) return;

	CachedAimData = AimData;
	bIsTriggerHeld = true;
	SetActorTickEnabled(true);

	HandleFire_Server();
}

void ATPMPWeapon::Server_StopFire_Implementation()
{
	bIsTriggerHeld = false;
	SetActorTickEnabled(false);
}

FVector ATPMPWeapon::GetMuzzleLocation() const
{
	if (WeaponMesh)
	{
		return WeaponMesh->GetSocketLocation(MuzzleSocketName);
	}
	return GetActorLocation();
}

FRotator ATPMPWeapon::GetMuzzleRotation() const
{
	if (WeaponMesh)
	{
		return WeaponMesh->GetSocketRotation(MuzzleSocketName);
	}
	return GetActorRotation();
}

void ATPMPWeapon::Server_UpdateAimPoint_Implementation(const FTPMPAimData& AimData)
{
	CachedAimData = AimData;
}

void ATPMPWeapon::HandleFire_Server()
{
	if (!HasAuthority() || !OwnerCharacter) return;

	const float Now = GetWorld()->GetTimeSeconds();
	if (Now - LastFireTime < FireRate) return;
	LastFireTime = Now;

	const FVector MuzzleLoc = GetMuzzleLocation();
	const FVector MuzzleForward = GetMuzzleRotation().Vector();

	// hitscan 判定（准星射线）
	FVector AimTarget;
	FVector ResolvedImpact;
	{
		FVector ScanStart = CachedAimData.Origin;
		FVector ScanDir;
		if (CachedAimData.Origin.IsNearlyZero() || CachedAimData.ImpactPoint.IsNearlyZero())
		{
			ScanStart = MuzzleLoc;
			ScanDir = MuzzleForward;
		}
		else
		{
			ScanDir = (CachedAimData.ImpactPoint - CachedAimData.Origin).GetSafeNormal();
		}
		const FVector ScanEnd = ScanStart + ScanDir * 50000.0f;

		FCollisionQueryParams QParams;
		QParams.AddIgnoredActor(this);
		QParams.AddIgnoredActor(OwnerCharacter);
		QParams.bTraceComplex = false;
		QParams.bReturnPhysicalMaterial = false;

		TArray<FHitResult> Hits;
		bool bResolved = false;
		if (GetWorld()->LineTraceMultiByChannel(Hits, ScanStart, ScanEnd, ECC_Visibility, QParams))
		{
			for (const FHitResult& H : Hits)
			{
				AActor* HitActor = H.GetActor();
				if (!HitActor || HitActor == OwnerCharacter || HitActor == this) continue;
				if (HitActor->IsA(AThirdPersonMPProjectile::StaticClass())) continue;

				ResolvedImpact = H.ImpactPoint;
				AimTarget = H.ImpactPoint;
				bResolved = true;

				// 造成伤害 
				AController* InstigatorCtrl = OwnerCharacter ? OwnerCharacter->GetController() : nullptr;
				UGameplayStatics::ApplyPointDamage(
					HitActor, Damage, ScanDir, H,
					InstigatorCtrl, this, UDamageType::StaticClass()
				);
				break;
			}
		}

		if (!bResolved)
		{
			ResolvedImpact = ScanEnd;
			AimTarget = ScanEnd;
		}
	}

	// 子弹从枪口飞向命中点
	const FRotator FireRot = (ResolvedImpact - MuzzleLoc).Rotation();
	if (ProjectileClass)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
		Params.Owner = OwnerCharacter;
		Params.Instigator = OwnerCharacter;
		GetWorld()->SpawnActor<AThirdPersonMPProjectile>(
			ProjectileClass, MuzzleLoc, FireRot, Params
		);
	}

	Multicast_PlayFireFX();
}

void ATPMPWeapon::Multicast_PlayFireFX_Implementation()
{
	if (CharacterFireMontage && OwnerCharacter && OwnerCharacter->GetMesh())
	{
		if (UAnimInstance* AI = OwnerCharacter->GetMesh()->GetAnimInstance())
		{
			AI->Montage_Play(CharacterFireMontage);
		}
	}
}