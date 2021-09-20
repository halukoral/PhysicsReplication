// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PhysicableComponent.generated.h"

USTRUCT()
struct FPhysicsState
{
	GENERATED_BODY()

	UPROPERTY()
	FTransform Transform;

	UPROPERTY()
	FVector Velocity;

	UPROPERTY()
	float	ServerDeltaTime;

	FPhysicsState()
	{
		Transform		= FTransform::Identity;
		Velocity		= FVector::ZeroVector;
		ServerDeltaTime	= 0.f;
	}
};

UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class PHYSICSREPLICATION_API UPhysicableComponent : public UActorComponent
{
	GENERATED_BODY()

public:	

	UPhysicableComponent();

protected:

	virtual void BeginPlay() override;

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	
public:	
	
	/**
	* Update actor data before the scene simulates
	*/
	void PreTick(FPhysScene* PhysScene, float DeltaTime);

	/**
	* Update actors data before the scene simulates
	*/
	void TickPhysics(FPhysScene* PhysScene, float DeltaTime);

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	virtual void RegisterComponentTickFunctions(bool bRegister) override;

	virtual	void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Stops movement immediately (zeroes velocity, usually zeros acceleration for components with acceleration). */
	void StopMovementImmediately();

	/** Read current state for simulation */
	void UpdateState(float DeltaTime);

protected:

	FBodyInstance* GetBodyInstance() const;

	/** Get our controller */
	APlayerController* GetController() const;
	
private:

	/**
	* The component we move and update.
	*/
	UPROPERTY(Transient, DuplicateTransient)
	AActor*					Owner;

	/**
	* UpdatedComponent, cast as a UPrimitiveComponent. May be invalid if UpdatedComponent was null or not a UPrimitiveComponent.
	*/
	UPROPERTY(Transient, DuplicateTransient)
	UPrimitiveComponent*	PrimitiveComponent;

	/** Current velocity of updated component. */
	FVector					Velocity;

	/**
	* If true, after registration we will add a tick dependency to tick before our owner (if we can both tick).
	* This is important when our tick causes an update in the owner's position, so that when the owner ticks it uses the most recent position without lag.
	* Disabling this can improve performance if both objects tick but the order of ticks doesn't matter.
	*/
	uint8					bTickBeforeOwner:1;

	UPROPERTY(Transient, Replicated)
	FPhysicsState			PhysicsState;
	
	FDelegateHandle 		OnPhysScenePreTickHandle;
	FDelegateHandle 		OnPhysSceneStepHandle;

};
