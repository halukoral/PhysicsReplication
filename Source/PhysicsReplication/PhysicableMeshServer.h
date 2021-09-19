// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PhysicableMeshServer.generated.h"

UCLASS()
class PHYSICSREPLICATION_API APhysicableMeshServer : public AActor
{
	GENERATED_BODY()
	
public:	

	APhysicableMeshServer();

protected:

	virtual void BeginPlay() override;

public:	

	virtual void Tick(float DeltaTime) override;

private:
	
	UPROPERTY(EditAnywhere)
	USceneComponent* Scene { nullptr };
	
	UPROPERTY(EditAnywhere)
	UStaticMeshComponent*	Mesh { nullptr };
};
