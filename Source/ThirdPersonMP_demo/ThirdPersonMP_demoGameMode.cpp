// Copyright Epic Games, Inc. All Rights Reserved.

#include "ThirdPersonMP_demoGameMode.h"
#include "ThirdPersonMP_demoCharacter.h"
#include "UObject/ConstructorHelpers.h"

AThirdPersonMP_demoGameMode::AThirdPersonMP_demoGameMode()
{
	// set default pawn class to our Blueprinted character
	static ConstructorHelpers::FClassFinder<APawn> PlayerPawnBPClass(TEXT("/Game/ThirdPerson/Blueprints/BP_ThirdPersonCharacter"));
	if (PlayerPawnBPClass.Class != NULL)
	{
		DefaultPawnClass = PlayerPawnBPClass.Class;
	}
}
