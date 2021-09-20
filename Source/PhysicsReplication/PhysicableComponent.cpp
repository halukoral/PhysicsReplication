// Fill out your copyright notice in the Description page of Project Settings.


#include "PhysicableComponent.h"

#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "Physics/PhysicsInterfaceCore.h"

UPhysicableComponent::UPhysicableComponent()
{
	PrimaryComponentTick.bCanEverTick = true;

	bTickBeforeOwner = true;
	SetIsReplicated(true);
}

void UPhysicableComponent::BeginPlay()
{
	Super::BeginPlay();

	Owner = GetOwner() ? GetOwner() : nullptr;
	PrimitiveComponent = Owner ? Cast<UPrimitiveComponent>(GetOwner()->GetRootComponent()) : nullptr;
	
	// Set up delegates
	OnPhysScenePreTickHandle = GetWorld()->GetPhysicsScene()->OnPhysScenePreTick.AddUObject(this, &UPhysicableComponent::PreTick);
	OnPhysSceneStepHandle = GetWorld()->GetPhysicsScene()->OnPhysSceneStep.AddUObject(this, &UPhysicableComponent::TickPhysics);
}

void UPhysicableComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	GetWorld()->GetPhysicsScene()->OnPhysScenePreTick.Remove(OnPhysScenePreTickHandle);
	GetWorld()->GetPhysicsScene()->OnPhysSceneStep.Remove(OnPhysSceneStepHandle);
}

void UPhysicableComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UPhysicableComponent, PhysicsState);
}

void UPhysicableComponent::PreTick(FPhysScene* PhysScene, float DeltaTime)
{
	if(PrimitiveComponent && PrimitiveComponent->IsSimulatingPhysics())
	{
		UpdateState(DeltaTime);
	}
}

void UPhysicableComponent::TickPhysics(FPhysScene* PhysScene, float DeltaTime)
{
	UE_LOG(LogTemp, Warning, TEXT("TickPhysics") );
}

void UPhysicableComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
		
	if (GetWorld()->GetNetMode() == NM_Client)
	{
		Owner->SetActorTransform(PhysicsState.Transform);

		/////////////////////////////////
		GEngine->AddOnScreenDebugMessage(-1, 15.0f, FColor::Red,
			FString::Printf(TEXT("Client %s"), *PhysicsState.Transform.ToString())
			);
		/////////////////////////////////
	}

}

void UPhysicableComponent::RegisterComponentTickFunctions(bool bRegister)
{
	Super::RegisterComponentTickFunctions(bRegister);

	// If the owner ticks, make sure we tick first
	if (bTickBeforeOwner && bRegister && PrimaryComponentTick.bCanEverTick && Owner && Owner->CanEverTick())
	{
		Owner->PrimaryActorTick.AddPrerequisite(this, PrimaryComponentTick);
	}
}

void UPhysicableComponent::UpdateState(float DeltaTime)
{
	if (GetWorld()->GetNetMode() != NM_Client)
	{
		PhysicsState.Transform = Owner->GetActorTransform();
		PhysicsState.Velocity = GetOwner()->GetVelocity();
		PhysicsState.ServerDeltaTime = DeltaTime;

		/////////////////////////////////
		GEngine->AddOnScreenDebugMessage(-1, 15.0f, FColor::Green,
			FString::Printf(TEXT("Server %s"), *Owner->GetActorTransform().ToString())
			);
		/////////////////////////////////
	}
}

void UPhysicableComponent::StopMovementImmediately()
{
	FBodyInstance* BodyInstance = GetBodyInstance();
	if (BodyInstance)
	{
		BodyInstance->SetLinearVelocity(FVector::ZeroVector, false);
		BodyInstance->SetAngularVelocityInRadians(FVector::ZeroVector, false);
		BodyInstance->ClearForces();
		BodyInstance->ClearTorques();
	}
}

FBodyInstance* UPhysicableComponent::GetBodyInstance() const
{
	return PrimitiveComponent ? PrimitiveComponent->GetBodyInstance() : nullptr;
}

APlayerController* UPhysicableComponent::GetController() const
{
	return UGameplayStatics::GetPlayerController(GetWorld(), 0);
}

