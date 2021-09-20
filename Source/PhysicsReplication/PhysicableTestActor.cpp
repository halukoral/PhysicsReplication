// Fill out your copyright notice in the Description page of Project Settings.

#include "PhysicableTestActor.h"
#include "PhysicableComponent.h"

APhysicableTestActor::APhysicableTestActor()
{
	PrimaryActorTick.bCanEverTick = true;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetCollisionProfileName(TEXT("BlockAll"));
	Mesh->SetSimulatePhysics(false);
	RootComponent = Mesh;
	
	PhysicableComponent = CreateDefaultSubobject<UPhysicableComponent>(TEXT("PhysicableComponent"));

	bReplicates = true;
}

void APhysicableTestActor::BeginPlay()
{
	Super::BeginPlay();

	if (GetLocalRole() < ROLE_Authority)
	{
		// Physics are NOT replicated. We will need to have actor orientation
		// replicated to us. We need to turn off physics simulation for clients.
		Mesh->SetSimulatePhysics(false);
		Mesh->SetEnableGravity(false);

		/////////////////////////////////
		GEngine->AddOnScreenDebugMessage(-1, 15.0f, FColor::Green,
			FString::Printf(TEXT("LAN NOLUYO YAA"))
			);
		/////////////////////////////////
	}
	else
	{
		NetUpdateFrequency = 1;
		Mesh->SetSimulatePhysics(true);
		Mesh->SetEnableGravity(true);
	}
}

void APhysicableTestActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

}

