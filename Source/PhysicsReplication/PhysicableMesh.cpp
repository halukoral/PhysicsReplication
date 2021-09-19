// Fill out your copyright notice in the Description page of Project Settings.


#include "PhysicableMesh.h"

APhysicableMesh::APhysicableMesh()
{
	PrimaryActorTick.bCanEverTick = true;

	Scene = CreateDefaultSubobject<USceneComponent>(TEXT("Scene"));
	RootComponent = Scene;
	
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(RootComponent);
	Mesh->SetSimulatePhysics(true);
	Mesh->SetEnableGravity(true);

	bReplicates = true;
	SetReplicateMovement(true);
	Mesh->SetIsReplicated(true);
}

void APhysicableMesh::BeginPlay()
{
	Super::BeginPlay();
	
}

void APhysicableMesh::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

}

