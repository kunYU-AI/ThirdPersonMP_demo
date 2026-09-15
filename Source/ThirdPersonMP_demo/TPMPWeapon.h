// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TPMPWeapon.generated.h"

class USkeletalMeshComponent;
class UAnimMontage;
class AThirdPersonMPProjectile;
class AThirdPersonMP_demoCharacter;

USTRUCT(BlueprintType)
struct FTPMPAimData
{
	GENERATED_BODY()

	UPROPERTY()
	FVector Origin = FVector::ZeroVector;

	UPROPERTY()
	FVector ImpactPoint = FVector::ZeroVector;
};

UCLASS(Abstract, Blueprintable)
class THIRDPERSONMP_DEMO_API ATPMPWeapon : public AActor
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	ATPMPWeapon();

	// 武器装备到玩家身上
	UFUNCTION(BlueprintCallable, Category = "Weapon")
	virtual void EquipTo(AThirdPersonMP_demoCharacter* NewOwner);

	// 射线检测
	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "Weapon|Fire")
	void Server_StartFire(const FTPMPAimData& AimData);

	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "Weapon|Fire")
	void Server_StopFire();

	// 高频上报瞄准数据
	UFUNCTION(Server, Unreliable, Category = "Weapon|Fire")
	void Server_UpdateAimPoint(const FTPMPAimData& AimData);

	// 开火动画
	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_PlayFireFX();

	UFUNCTION(BlueprintPure, Category = "Weapon")
	FVector GetMuzzleLocation() const;

	UFUNCTION(BlueprintPure, Category = "Weapon")
	FRotator GetMuzzleRotation() const;

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

	// 服务器：执行一次发射
	void HandleFire_Server();

	// 客户端传过来的瞄准数据
	FTPMPAimData CachedAimData;

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	
	// 武器的骨骼网络
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	USkeletalMeshComponent* WeaponMesh;

	// 在角色的骨骼上挂武器的Socket名
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Config")
	FName CharacterAttachSocketName;

	// 蓝图的配置
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Config")
	FName MuzzleSocketName;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Config")
	TSubclassOf<AThirdPersonMPProjectile> ProjectileClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Config")
	float FireRate;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Config")
	float Damage;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Config")
	bool bIsAutomatic;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Weapon|State")
	bool bIsTriggerHeld;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Weapon|State")
	AThirdPersonMP_demoCharacter* OwnerCharacter;

	// 蓝图配置特效资产地方
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Animation")
	UAnimMontage* CharacterFireMontage;

protected:
	// 上次发射事件
	float LastFireTime;
};
