// Fill out your copyright notice in the Description page of Project Settings.


#include "PhysicableMeshServer.h"

APhysicableMeshServer::APhysicableMeshServer()
{
	PrimaryActorTick.bCanEverTick = true;
	
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	RootComponent = Mesh;
	
	bReplicates = true;
	SetReplicateMovement(true);
	Mesh->SetIsReplicated(true);
}

void APhysicableMeshServer::BeginPlay()
{
	Super::BeginPlay();

	if (GetLocalRole() < ROLE_Authority)
	{
		// Physics are NOT replicated. We will need to have actor orientation
		// replicated to us. We need to turn off physics simulation for clients
		Mesh->SetSimulatePhysics(false);
		Mesh->SetEnableGravity(false);
	}
	else
	{
		Mesh->SetSimulatePhysics(true);
		Mesh->SetEnableGravity(true);
	}
}

void APhysicableMeshServer::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

}

