// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Physicable.generated.h"

USTRUCT()
struct FPhysicsState
{
	GENERATED_BODY()

	UPROPERTY()
	FTransform Transform;

	UPROPERTY()
	FVector Velocity;

	UPROPERTY()
	float	DeltaTime;

	FPhysicsState()
	{
		Transform		= FTransform::Identity;
		Velocity		= FVector::ZeroVector;
		DeltaTime		= 0.f;
	}
};

struct FHermiteCubicSpline
{
	FVector StartLocation, StartDerivative, TargetLocation, TargetDerivative;

	FVector InterpolateLocation(const float& LerpRatio) const
	{
		return FMath::CubicInterp(StartLocation, StartDerivative, TargetLocation, TargetDerivative, LerpRatio);
	}
	
	FVector InterpolateDerivative(const float& LerpRatio) const
	{
		return FMath::CubicInterpDerivative(StartLocation, StartDerivative, TargetLocation, TargetDerivative, LerpRatio);
	}
};

UCLASS()
class PHYSICSREPLICATION_API APhysicable : public AActor
{
	GENERATED_BODY()
	
public:	

	APhysicable();

protected:

	virtual void			BeginPlay() override;
		
public:		
		
	virtual	void			GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
		
	virtual void			Tick(float DeltaTime) override;
		
	void					ServerTick(float DeltaTime);
	
	void					ClientTick(float DeltaTime);
		
	void					UpdatePhysicsState(float DeltaTime);
		
	void 					InterpolateLocation(const FHermiteCubicSpline& Spline, const float& LerpRatio) const;
		
	void 					InterpolateVelocity(const FHermiteCubicSpline& Spline, const float& LerpRatio) const;
		
	void 					InterpolateRotation(const float& LerpRatio) const;
		
	float					VelocityToDerivative() const;
	
	void 					SimulatedProxy_PhysicsState();
	
	FHermiteCubicSpline		CreateSpline() const;

	UFUNCTION()
	void					OnRep_PhysicsState();	/** The object orientation on the server */
	
	UPROPERTY(ReplicatedUsing = OnRep_PhysicsState)
	FPhysicsState			PhysicsState;
	
private:

	TArray<FPhysicsState>	UnacknowledgedPhysicsStates;

	
	float 					ClientSimulatedTime { 0 };
					
	float 					ClientTimeSinceUpdate { 0 };
					
	float 					ClientTimeBetweenLastUpdates { 0 };

	FTransform				ClientTransform { FTransform::Identity };
	
	FVector					ClientStartVelocity { FVector::ZeroVector };

	UPROPERTY(Replicated)
	FVector					LastVelocity { FVector::ZeroVector };

	UPROPERTY(Replicated)
	FVector					VelocityDifference { FVector::ZeroVector };
	
	UPROPERTY(EditAnywhere)
	USceneComponent* Scene { nullptr };
	
	UPROPERTY(EditAnywhere)
	UStaticMeshComponent*	Mesh { nullptr };
};
