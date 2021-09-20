// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "PhysicsMovementReplication.h"
#include "Components/ActorComponent.h"
#include "Interfaces/NetworkPredictionInterface.h"
#include "PhysicsMovementComponent.generated.h"


class FNetworkPredictionData_Client_Physics;

/** Shared pointer for easy memory management of FSavedMove_Character, for accumulating and replaying network moves. */
typedef TSharedPtr<class FSavedMove_Physics> FSavedPhysicsMovePtr;

class PHYSICSREPLICATION_API FSavedMove_Physics
{
	
public:
	FSavedMove_Physics()
	{
		AccelMagThreshold = 1.f;
		AccelDotThreshold = 0.9f;
		AccelDotThresholdCombine = 0.996f; // approx 5 degrees.
		MaxSpeedThresholdCombine = 10.0f;
	}
	
	virtual ~FSavedMove_Physics() {}
	
	float TimeStamp;    // Time of this move.
	float DeltaTime;    // amount of time for this move
	float CustomTimeDilation;
	
	// Information at the start of the move
	uint8 StartPackedMovementMode;
	FVector StartLocation;
	FVector StartRelativeLocation;
	FVector StartVelocity;
	FRotator StartRotation;
	FRotator StartControlRotation;
	FQuat StartBaseRotation;	// rotation of the base component (or bone), only saved if it can move.
	TWeakObjectPtr<UPrimitiveComponent> StartBase;

	// Information after the move has been performed
	uint8 EndPackedMovementMode;
	FVector SavedLocation;
	FRotator SavedRotation;
	FVector SavedVelocity;
	FVector SavedRelativeLocation;
	FRotator SavedControlRotation;
	TWeakObjectPtr<UPrimitiveComponent> EndBase;

	FVector Acceleration;
	float MaxSpeed;

	// Cached to speed up iteration over IsImportantMove().
	FVector AccelNormal;
	float AccelMag;

	/** Threshold for deciding this is an "important" move based on DP with last acked acceleration. */
	float AccelDotThreshold;    
	/** Threshold for deciding is this is an important move because acceleration magnitude has changed too much */
	float AccelMagThreshold;	
	/** Threshold for deciding if we can combine two moves, true if cosine of angle between them is <= this. */
	float AccelDotThresholdCombine;
	/** Client saved moves will not combine if the result of GetMaxSpeed() differs by this much between moves. */
	float MaxSpeedThresholdCombine;
	
	/** Clear saved move properties, so it can be re-used. */
	virtual void Clear();

		/** Called to set up this saved move (when initially created) to make a predictive correction. */
	virtual void SetMoveFor(ACharacter* C, float InDeltaTime, FVector const& NewAccel, class FNetworkPredictionData_Client_Character & ClientData);

	/** Set the properties describing the position, etc. of the moved pawn at the start of the move. */
	virtual void SetInitialPosition(ACharacter* C);

	/** Returns true if this move is an "important" move that should be sent again if not acked by the server */
	virtual bool IsImportantMove(const FSavedPhysicsMovePtr& LastAckedMove) const;
	
	/** Returns starting position if we were to revert the move, either absolute StartLocation, or StartRelativeLocation offset from MovementBase's current location (since we want to try to move forward at this time). */
	virtual FVector GetRevertedLocation() const;

	enum EPostUpdateMode
	{
		PostUpdate_Record,		// Record a move after having run the simulation
		PostUpdate_Replay,		// Update after replaying a move for a client correction
	};

	/** Set the properties describing the final position, etc. of the moved pawn. */
	virtual void PostUpdate(ACharacter* C, EPostUpdateMode PostUpdateMode);
	
	/** Returns true if this move can be combined with NewMove for replication without changing any behavior */
	virtual bool CanCombineWith(const FSavedPhysicsMovePtr& NewMove, ACharacter* InCharacter, float MaxDelta) const;

	/** Combine this move with an older move and update relevant state. */
	virtual void CombineWith(const FSavedMove_Physics* OldMove, ACharacter* InCharacter, APlayerController* PC, const FVector& OldStartLocation);
	
	/** Called before ClientUpdatePosition uses this SavedMove to make a predictive correction	 */
	virtual void PrepMoveFor(ACharacter* C);

	/** Compare current control rotation with stored starting data */
	virtual bool IsMatchingStartControlRotation(const APlayerController* PC) const;

	/** Packs control rotation for network transport */
	virtual void GetPackedAngles(uint32& YawAndPitchPack, uint8& RollPack) const;
};

UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class PHYSICSREPLICATION_API UPhysicsMovementComponent : public UActorComponent//, public INetworkPredictionInterface
{
	GENERATED_BODY()

public:	

	UPhysicsMovementComponent();

	/**
	* On the client, calls the ServerMovePacked_ClientSend() function with packed movement data.
	* First the FCharacterNetworkMoveDataContainer from GetNetworkMoveDataContainer() is updated with ClientFillNetworkMoveData(), then serialized into a data stream to send client player moves to the server.
	*/
	virtual void CallServerMovePacked(const FSavedMove_Physics* NewMove, const FSavedMove_Physics* PendingMove, const FSavedMove_Physics* OldMove);

	/** Combine this move with an older move and update relevant state. */
	virtual void CombineWith(const FSavedMove_Physics* OldMove, const FVector& OldStartLocation);

	
	/** Return true if it is OK to delay sending this player movement to the server, in order to conserve bandwidth. */
	virtual bool CanDelaySendingMove(const FSavedPhysicsMovePtr& NewMove);

	/** Determine minimum delay between sending client updates to the server. If updates occur more frequently this than this time, moves may be combined delayed. */
	virtual float GetClientNetSendDeltaTime(const FNetworkPredictionData_Client_Physics* ClientData, const FSavedPhysicsMovePtr& NewMove) const;

	static uint32 PackYawAndPitchTo32(const float Yaw, const float Pitch);
};

FORCEINLINE uint32 UPhysicsMovementComponent::PackYawAndPitchTo32(const float Yaw, const float Pitch)
{
	const uint32 YawShort = FRotator::CompressAxisToShort(Yaw);
	const uint32 PitchShort = FRotator::CompressAxisToShort(Pitch);
	const uint32 Rotation32 = (YawShort << 16) | PitchShort;
	return Rotation32;
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class FCharacterReplaySample
{
public:
	FCharacterReplaySample() : RemoteViewPitch( 0 ), Time( 0.0f )
	{
	}

	friend ENGINE_API FArchive& operator<<( FArchive& Ar, FCharacterReplaySample& V );

	FVector			Location;
	FRotator		Rotation;
	FVector			Velocity;
	FVector			Acceleration;
	uint8			RemoteViewPitch;
	float			Time;					// This represents time since replay started
};

class ENGINE_API FNetworkPredictionData_Client_Physics : public FNetworkPredictionData_Client, protected FNoncopyable
{
public:

	FNetworkPredictionData_Client_Physics();
	virtual ~FNetworkPredictionData_Client_Physics();

	/** Client timestamp of last time it sent a servermove() to the server. This is an increasing timestamp from the owning UWorld. Used for holding off on sending movement updates to save bandwidth. */
	float ClientUpdateTime;

	/** Current TimeStamp for sending new Moves to the Server. This time resets to zero at a frequency of MinTimeBetweenTimeStampResets. */
	float CurrentTimeStamp;

	/** Last World timestamp (undilated, real time) at which we received a server ack for a move. This could be either a good move or a correction from the server. */
	float LastReceivedAckRealTime;

	TArray<FSavedPhysicsMovePtr> SavedMoves;		// Buffered moves pending position updates, orderd oldest to newest. Moves that have been acked by the server are removed.
	TArray<FSavedPhysicsMovePtr> FreeMoves;		// freed moves, available for buffering
	FSavedPhysicsMovePtr PendingMove;				// PendingMove already processed on client - waiting to combine with next movement to reduce client to server bandwidth
	FSavedPhysicsMovePtr LastAckedMove;			// Last acknowledged sent move.

	int32 MaxFreeMoveCount;					// Limit on size of free list
	int32 MaxSavedMoveCount;				// Limit on the size of the saved move buffer

	uint32 bUpdatePosition:1; // when true, update the position (via ClientUpdatePosition)

	// Mesh smoothing variables (for network smoothing)
	//


	/** Used for position smoothing in net games */
	FVector OriginalMeshTranslationOffset;

	/** World space offset of the mesh. Target value is zero offset. Used for position smoothing in net games. */
	FVector MeshTranslationOffset;

	/** Used for rotation smoothing in net games (only used by linear smoothing). */
	FQuat OriginalMeshRotationOffset;

	/** Component space offset of the mesh. Used for rotation smoothing in net games. */
	FQuat MeshRotationOffset;

	/** Target for mesh rotation interpolation. */
	FQuat MeshRotationTarget;

	/** Used for remembering how much time has passed between server corrections */
	float LastCorrectionDelta;

	/** Used to track time of last correction */
	float LastCorrectionTime;

	/** Max time delta between server updates over which client smoothing is allowed to interpolate. */
	float MaxClientSmoothingDeltaTime;

	/** Used to track the timestamp of the last server move. */
	double SmoothingServerTimeStamp;

	/** Used to track the client time as we try to match the server.*/
	double SmoothingClientTimeStamp;

	/**
	 * Copied value from UCharacterMovementComponent::NetworkMaxSmoothUpdateDistance.
	 * @see UCharacterMovementComponent::NetworkMaxSmoothUpdateDistance
	 */
	float MaxSmoothNetUpdateDist;

	/**
	 * Copied value from UCharacterMovementComponent::NetworkNoSmoothUpdateDistance.
	 * @see UCharacterMovementComponent::NetworkNoSmoothUpdateDistance
	 */
	float NoSmoothNetUpdateDist;

	/** How long to take to smoothly interpolate from the old pawn position on the client to the corrected one sent by the server.  Must be >= 0. Not used for linear smoothing. */
	float SmoothNetUpdateTime;

	/** How long to take to smoothly interpolate from the old pawn rotation on the client to the corrected one sent by the server.  Must be >= 0. Not used for linear smoothing. */
	float SmoothNetUpdateRotationTime;
	
	/** 
	 * Max delta time for a given move, in real seconds
	 * Based off of AGameNetworkManager::MaxMoveDeltaTime config setting, but can be modified per actor
	 * if needed.
	 * This value is mirrored in FNetworkPredictionData_Server, which is what server logic runs off of.
	 * Client needs to know this in order to not send move deltas that are going to get clamped anyway (meaning
	 * they'll be rejected/corrected).
	 * Note: This was previously named MaxResponseTime, but has been renamed to reflect what it does more accurately
	 */
	float MaxMoveDeltaTime;

	/** Values used for visualization and debugging of simulated net corrections */
	FVector LastSmoothLocation;
	FVector LastServerLocation;
	float	SimulatedDebugDrawTime;

	/** Array of replay samples that we use to interpolate between to get smooth location/rotation/velocity/ect */
	TArray< FCharacterReplaySample > ReplaySamples;

	/** Finds SavedMove index for given TimeStamp. Returns INDEX_NONE if not found (move has been already Acked or cleared). */
	int32 GetSavedMoveIndex(float TimeStamp) const;

	/** Ack a given move. This move will become LastAckedMove, SavedMoves will be adjusted to only contain unAcked moves. */
	void AckMove(int32 AckedMoveIndex);

	/** Allocate a new saved move. Subclasses should override this if they want to use a custom move class. */
	virtual FSavedPhysicsMovePtr AllocateNewMove();

	/** Return a move to the free move pool. Assumes that 'Move' will no longer be referenced by anything but possibly the FreeMoves list. Clears PendingMove if 'Move' is PendingMove. */
	virtual void FreeMove(const FSavedPhysicsMovePtr& Move);

	/** Tries to pull a pooled move off the free move list, otherwise allocates a new move. Returns NULL if the limit on saves moves is hit. */
	virtual FSavedPhysicsMovePtr CreateSavedMove();

	/** Update CurentTimeStamp from passed in DeltaTime.
		It will measure the accuracy between passed in DeltaTime and how Server will calculate its DeltaTime.
		If inaccuracy is too high, it will reset CurrentTimeStamp to maintain a high level of accuracy.
		@return DeltaTime to use for Client's physics simulation prior to replicate move to server. */
	float UpdateTimeStampAndDeltaTime(float DeltaTime, ACharacter & CharacterOwner, class UCharacterMovementComponent & CharacterMovementComponent);

	/** Used for simulated packet loss in development builds. */
	float DebugForcedPacketLossTimerStart;
};


class ENGINE_API FNetworkPredictionData_Server_Physics : public FNetworkPredictionData_Server, protected FNoncopyable
{
public:

	FNetworkPredictionData_Server_Physics(const UCharacterMovementComponent& ServerMovement);
	virtual ~FNetworkPredictionData_Server_Physics();

	FClientAdjustmentPhysic PendingAdjustment;

	/** Timestamp from the client of most recent ServerMove() processed for this player. Reset occasionally for timestamp resets (to maintain accuracy). */
	float CurrentClientTimeStamp;

	/** Timestamp from the client of most recent ServerMove() received for this player, including rejected requests. */
	float LastReceivedClientTimeStamp;

	/** Timestamp of total elapsed client time. Similar to CurrentClientTimestamp but this is accumulated with the calculated DeltaTime for each move on the server. */
	double ServerAccumulatedClientTimeStamp;

	/** Last time server updated client with a move correction */
	float LastUpdateTime;

	/** Server clock time when last server move was received from client (does NOT include forced moves on server) */
	float ServerTimeStampLastServerMove;

	/** 
	 * Max delta time for a given move, in real seconds
	 * Based off of AGameNetworkManager::MaxMoveDeltaTime config setting, but can be modified per actor
	 * if needed.
	 * Note: This was previously named MaxResponseTime, but has been renamed to reflect what it does more accurately
	 */
	float MaxMoveDeltaTime;

	/** Force client update on the next ServerMoveHandleClientError() call. */
	uint32 bForceClientUpdate:1;

	/** Accumulated timestamp difference between autonomous client and server for tracking long-term trends */
	float LifetimeRawTimeDiscrepancy;

	/** 
	 * Current time discrepancy between client-reported moves and time passed
	 * on the server. Time discrepancy resolution's goal is to keep this near zero.
	 */
	float TimeDiscrepancy;

	/** True if currently in the process of resolving time discrepancy */
	bool bResolvingTimeDiscrepancy;

	/** 
	 * When bResolvingTimeDiscrepancy is true, we are in time discrepancy resolution mode whose output is
	 * this value (to be used as the DeltaTime for current ServerMove)
	 */
	float TimeDiscrepancyResolutionMoveDeltaOverride;

	/** 
	 * When bResolvingTimeDiscrepancy is true, we are in time discrepancy resolution mode where we bound
	 * move deltas by Server Deltas. In cases where there are multiple ServerMove RPCs handled within one
	 * server frame tick, we need to accumulate the client deltas of the "no tick" Moves so that the next
	 * Move processed that the server server has ticked for takes into account those previous deltas. 
	 * If we did not use this, the higher the framerate of a client vs the server results in more 
	 * punishment/payback time.
	 */
	float TimeDiscrepancyAccumulatedClientDeltasSinceLastServerTick;

	/** Creation time of this prediction data, used to contextualize LifetimeRawTimeDiscrepancy */
	float WorldCreationTime;

	/** Returns time delta to use for the current ServerMove(). Takes into account time discrepancy resolution if active. */
	float GetServerMoveDeltaTime(float ClientTimeStamp, float ActorTimeDilation) const;

	/** Returns base time delta to use for a ServerMove, default calculation (no time discrepancy resolution) */
	float GetBaseServerMoveDeltaTime(float ClientTimeStamp, float ActorTimeDilation) const;

};

