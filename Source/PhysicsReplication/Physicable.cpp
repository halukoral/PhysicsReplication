// Fill out your copyright notice in the Description page of Project Settings.


#include "Physicable.h"

#include "Net/UnrealNetwork.h"

APhysicable::APhysicable()
{
	PrimaryActorTick.bCanEverTick = true;

	Scene = CreateDefaultSubobject<USceneComponent>(TEXT("Scene"));
	
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetCollisionProfileName(TEXT("BlockAll"));
	Mesh->SetupAttachment(Scene);
	Mesh->SetSimulatePhysics(false);

	RootComponent = Scene;

	bReplicates = true;
	SetReplicateMovement(false);
}

void APhysicable::BeginPlay()
{
	Super::BeginPlay();

	if (GetLocalRole() < ROLE_Authority)
	{
		// Physics are NOT replicated. We will need to have actor orientation
		// replicated to us. We need to turn off physics simulation for clients.
		Mesh->SetSimulatePhysics(false);
		Mesh->SetEnableGravity(false);
	}
	else
	{
		NetUpdateFrequency = 1;
		Mesh->SetSimulatePhysics(true);
		Mesh->SetEnableGravity(true);
	}
}

void APhysicable::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(APhysicable, PhysicsState);
	DOREPLIFETIME(APhysicable, LastVelocity);
	DOREPLIFETIME(APhysicable, VelocityDifference);
}

void APhysicable::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	UE_LOG(LogTemp, Warning, TEXT("%s"), *VelocityDifference.ToString());
			
	if (GetLocalRole() == ROLE_Authority)
	{
		if(Mesh->GetPhysicsLinearVelocity().SizeSquared() > 10.f)
		{
			VelocityDifference = LastVelocity - Mesh->GetPhysicsLinearVelocity();
			LastVelocity = Mesh->GetPhysicsLinearVelocity();
			ServerTick(DeltaTime);
		}
		else
		{
			VelocityDifference = FVector::ZeroVector;
			LastVelocity = FVector::ZeroVector;
		}
	}
	else if(GetLocalRole() == ROLE_SimulatedProxy)
	{
		ClientTick(DeltaTime);
	}
}

void APhysicable::ServerTick(float DeltaTime)
{
	UpdatePhysicsState(DeltaTime);
}

void APhysicable::ClientTick(float DeltaTime)
{
	ClientTimeSinceUpdate += DeltaTime;

	if (ClientTimeBetweenLastUpdates < KINDA_SMALL_NUMBER) return;

	const float LerpRatio = ClientTimeSinceUpdate / ClientTimeBetweenLastUpdates;

	const FHermiteCubicSpline Spline = CreateSpline();

	InterpolateLocation(Spline, LerpRatio);

	InterpolateRotation(LerpRatio);
}

void APhysicable::UpdatePhysicsState(float DeltaTime)
{
	PhysicsState.Transform  = Mesh->GetComponentTransform();
	PhysicsState.Velocity	= VelocityDifference;
	PhysicsState.DeltaTime	= DeltaTime;
}

FHermiteCubicSpline APhysicable::CreateSpline() const
{
	FHermiteCubicSpline Spline;
	Spline.StartLocation = ClientTransform.GetLocation();
	Spline.TargetLocation = PhysicsState.Transform.GetLocation();
	Spline.StartDerivative = ClientStartVelocity * VelocityToDerivative();
	Spline.TargetDerivative = PhysicsState.Velocity * VelocityToDerivative();
	return Spline;
}

void APhysicable::InterpolateLocation(const FHermiteCubicSpline& Spline, const float& LerpRatio) const
{
	Mesh->SetWorldLocation(Spline.InterpolateLocation(LerpRatio));
}

void APhysicable::InterpolateVelocity(const FHermiteCubicSpline& Spline, const float& LerpRatio) const
{
	const FVector NewDerivative = Spline.InterpolateDerivative(LerpRatio);
	const FVector NewVelocity = NewDerivative / VelocityToDerivative();
	Mesh->SetPhysicsLinearVelocity( NewVelocity );
}

void APhysicable::InterpolateRotation(const float& LerpRatio) const
{
	const FQuat TargetRotation = PhysicsState.Transform.GetRotation();
	const FQuat StartRotation = ClientTransform.GetRotation();
	const FQuat NewRotation = FQuat::Slerp(StartRotation, TargetRotation, LerpRatio);
	Mesh->SetWorldRotation(NewRotation);
}

float APhysicable::VelocityToDerivative() const
{
	return ClientTimeBetweenLastUpdates * 100;
}

void APhysicable::OnRep_PhysicsState()
{
	if(GetLocalRole() == ROLE_SimulatedProxy)
	{
		SimulatedProxy_PhysicsState();
	}
}

void APhysicable::SimulatedProxy_PhysicsState()
{
	ClientTimeBetweenLastUpdates = ClientTimeSinceUpdate;
	ClientTimeSinceUpdate = 0;

	ClientTransform.SetLocation(Mesh->GetComponentLocation());
	ClientTransform.SetRotation(Mesh->GetComponentQuat());
	
	ClientStartVelocity = VelocityDifference;

	Mesh->SetWorldLocation(PhysicsState.Transform.GetLocation());
	Mesh->SetWorldRotation(PhysicsState.Transform.GetRotation());
}
