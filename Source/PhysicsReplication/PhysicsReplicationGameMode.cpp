// Copyright Epic Games, Inc. All Rights Reserved.

#include "PhysicsReplicationGameMode.h"
#include "PhysicsReplicationHUD.h"
#include "PhysicsReplicationCharacter.h"
#include "UObject/ConstructorHelpers.h"

APhysicsReplicationGameMode::APhysicsReplicationGameMode() : Super()
{
	// set default pawn class to our Blueprinted character
	static ConstructorHelpers::FClassFinder<APawn> PlayerPawnClassFinder(TEXT("/Game/StarterContent/FirstPersonCPP/Blueprints/FirstPersonCharacter"));
	DefaultPawnClass = PlayerPawnClassFinder.Class;

	// use our custom HUD class
	HUDClass = APhysicsReplicationHUD::StaticClass();
}
