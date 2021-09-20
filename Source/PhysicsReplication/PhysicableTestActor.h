// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PhysicableTestActor.generated.h"

class UPhysicsMovementComponent;
UCLASS()
class PHYSICSREPLICATION_API APhysicableTestActor : public AActor
{
	GENERATED_BODY()
	
public:	

	APhysicableTestActor();

protected:

	virtual void BeginPlay() override;

public:	

	virtual void Tick(float DeltaTime) override;

private:

	UPROPERTY(EditAnywhere)
	UStaticMeshComponent*	Mesh { nullptr };
	
	UPhysicsMovementComponent*	PhysicableComponent { nullptr };
};
