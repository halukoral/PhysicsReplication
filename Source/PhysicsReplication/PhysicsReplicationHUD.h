// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "PhysicsReplicationHUD.generated.h"

UCLASS()
class APhysicsReplicationHUD : public AHUD
{
	GENERATED_BODY()

public:
	APhysicsReplicationHUD();

	/** Primary draw call for the HUD */
	virtual void DrawHUD() override;

private:
	/** Crosshair asset pointer */
	class UTexture2D* CrosshairTex;

};

