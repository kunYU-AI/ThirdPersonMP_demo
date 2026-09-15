// Fill out your copyright notice in the Description page of Project Settings.


#include "TPMPRifle.h"

// Sets default values
ATPMPRifle::ATPMPRifle()
{
	FireRate = 0.1f;
	Damage = 1.0f;
	bIsAutomatic = true;
	MuzzleSocketName = TEXT("Muzzle");           // SK_Rifle µÄÇ¹¿Ú socket Ãû
	CharacterAttachSocketName = TEXT("hand_r_socket");
}

