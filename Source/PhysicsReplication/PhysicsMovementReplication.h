// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/NetSerialization.h"
#include "Containers/BitArray.h"
#include "PhysicsMovementReplication.generated.h"

class UPackageMap;
class FSavedMove_Physics;
class UPhysicsMovementComponent;

// Number of bits to reserve in serialization container. Make this large enough to try to avoid re-allocation during the worst case RPC calls (dual move + unacknowledged "old important" move).
#ifndef PHYSICS_SERIALIZATION_PACKEDBITS_RESERVED_SIZE
#define PHYSICS_SERIALIZATION_PACKEDBITS_RESERVED_SIZE 1024
#endif

//////////////////////////////////////////////////////////////////////////
/**
 * Intermediate data stream used for network serialization of Character RPC data.
 * This is basically an array of bits that is packed/unpacked via NetSerialize into custom data structs on the sending and receiving ends.
 */
USTRUCT()
struct PHYSICSREPLICATION_API FPhysicNetworkSerializationPackedBits
{
	GENERATED_BODY()

	FPhysicNetworkSerializationPackedBits()
		: SavedPackageMap(nullptr)
	{
	}

	bool NetSerialize(FArchive& Ar, UPackageMap* PackageMap, bool& bOutSuccess);
	UPackageMap* GetPackageMap() const { return SavedPackageMap; }

	//------------------------------------------------------------------------
	// Data

	// TInlineAllocator used with TBitArray takes the number of 32-bit dwords, but the define is in number of bits, so convert here by dividing by 32.
	TBitArray<TInlineAllocator<PHYSICS_SERIALIZATION_PACKEDBITS_RESERVED_SIZE / NumBitsPerDWORD>> DataBits;

private:
	UPackageMap* SavedPackageMap;
};

template<>
struct TStructOpsTypeTraits<FPhysicNetworkSerializationPackedBits> : public TStructOpsTypeTraitsBase2<FPhysicNetworkSerializationPackedBits>
{
	enum
	{
		WithNetSerializer = true,
	};
};


//////////////////////////////////////////////////////////////////////////
// Client to Server movement data
//////////////////////////////////////////////////////////////////////////

/**
 * FPhysicNetworkMoveData encapsulates a client move that is sent to the server for UCharacterMovementComponent networking.
 *
 * Adding custom data to the network move is accomplished by deriving from this struct, adding new data members, implementing ClientFillNetworkMoveData(), implementing Serialize(), 
 * and setting up the UCharacterMovementComponent to use an instance of a custom FPhysicNetworkMoveDataContainer (see that struct for more details).
 * 
 * @see FPhysicNetworkMoveDataContainer
 */

struct PHYSICSREPLICATION_API FPhysicNetworkMoveData
{
public:

	enum class ENetworkMoveType
	{
		NewMove,
		PendingMove,
		OldMove
	};

	FPhysicNetworkMoveData()
		: NetworkMoveType(ENetworkMoveType::NewMove)
		, TimeStamp(0.f)
		, Acceleration(ForceInitToZero)
		, Location(ForceInitToZero)
		, ControlRotation(ForceInitToZero)
		, CompressedMoveFlags(0)
		, MovementBase(nullptr)
		, MovementMode(0)
	{
	}
	
	virtual ~FPhysicNetworkMoveData()
	{
	}

	/**
	 * Given a FSavedMove_Character from UCharacterMovementComponent, fill in data in this struct with relevant movement data.
	 * Note that the instance of the FSavedMove_Character is likely a custom struct of a derived struct of your own, if you have added your own saved move data.
	 * @see UCharacterMovementComponent::AllocateNewMove()
	 */
	virtual void ClientFillNetworkMoveData(const FSavedMove_Physics& ClientMove, ENetworkMoveType MoveType);

	/**
	 * Serialize the data in this struct to or from the given FArchive. This packs or unpacks the data in to a variable-sized data stream that is sent over the
	 * network from client to server.
	 * @see UCharacterMovementComponent::CallServerMovePacked
	 */
	virtual bool Serialize(UPhysicsMovementComponent& PhysicsMovement, FArchive& Ar, UPackageMap* PackageMap, ENetworkMoveType MoveType);

	// Indicates whether this was the latest new move, a pending/dual move, or old important move.
	ENetworkMoveType NetworkMoveType;

	//------------------------------------------------------------------------
	// Basic movement data.

	float TimeStamp;
	FVector_NetQuantize10 Acceleration;
	FVector_NetQuantize100 Location;		// Either world location or relative to MovementBase if that is set.
	FRotator ControlRotation;
	uint8 CompressedMoveFlags;

	class UPrimitiveComponent* MovementBase;
	uint8 MovementMode;
};


//////////////////////////////////////////////////////////////////////////
/**
 * Struct used for network RPC parameters between client/server by ACharacter and UCharacterMovementComponent.
 * To extend network move data and add custom parameters, you typically override this struct with a custom derived struct and set the CharacterMovementComponent
 * to use your container with UCharacterMovementComponent::SetNetworkMoveDataContainer(). Your derived struct would then typically (in the constructor) replace the
 * NewMoveData, PendingMoveData, and OldMoveData pointers to use your own instances of a struct derived from FPhysicNetworkMoveData, where you add custom fields
 * and implement custom serialization to be able to pack and unpack your own additional data.
 * 
 * @see UCharacterMovementComponent::SetNetworkMoveDataContainer()
 */
struct PHYSICSREPLICATION_API FPhysicNetworkMoveDataContainer
{
public:

	/**
	 * Default constructor. Sets data storage (NewMoveData, PendingMoveData, OldMoveData) to point to default data members. Override those pointers to instead point to custom data if you want to use derived classes.
	 */
	FPhysicNetworkMoveDataContainer()
		: bHasPendingMove(false)
		, bIsDualHybridRootMotionMove(false)
		, bHasOldMove(false)
		, bDisableCombinedScopedMove(false)
	{
		NewMoveData		= &BaseDefaultMoveData[0];
		PendingMoveData	= &BaseDefaultMoveData[1];
		OldMoveData		= &BaseDefaultMoveData[2];
	}

	virtual ~FPhysicNetworkMoveDataContainer()
	{
	}

	/**
	 * Passes through calls to ClientFillNetworkMoveData on each FPhysicNetworkMoveData matching the client moves. Note that ClientNewMove will never be null, but others may be.
	 */
	virtual void ClientFillNetworkMoveData(const FSavedMove_Physics* ClientNewMove, const FSavedMove_Physics* ClientPendingMove, const FSavedMove_Physics* ClientOldMove);

	/**
	 * Serialize movement data. Passes Serialize calls to each FPhysicNetworkMoveData as applicable, based on bHasPendingMove and bHasOldMove.
	 */
	virtual bool Serialize(UPhysicsMovementComponent& CharacterMovement, FArchive& Ar, UPackageMap* PackageMap);

	//------------------------------------------------------------------------
	// Basic movement data. NewMoveData is the most recent move, PendingMoveData is a move right before it (dual move). OldMoveData is an "important" move not yet acknowledged.

	FORCEINLINE FPhysicNetworkMoveData* GetNewMoveData() const		{ return NewMoveData; }
	FORCEINLINE FPhysicNetworkMoveData* GetPendingMoveData() const	{ return PendingMoveData; }
	FORCEINLINE FPhysicNetworkMoveData* GetOldMoveData() const		{ return OldMoveData; }

	//------------------------------------------------------------------------
	// Optional pending data used in "dual moves".
	bool bHasPendingMove;
	bool bIsDualHybridRootMotionMove;
	
	// Optional "old move" data, for redundant important old moves not yet ack'd.
	bool bHasOldMove;

	// True if we want to disable a scoped move around both dual moves (optional from bEnableServerDualMoveScopedMovementUpdates), typically set if bForceNoCombine was true which can indicate an important change in moves.
	bool bDisableCombinedScopedMove;
	
protected:

	FPhysicNetworkMoveData* NewMoveData;
	FPhysicNetworkMoveData* PendingMoveData;	// Only valid if bHasPendingMove is true
	FPhysicNetworkMoveData* OldMoveData;		// Only valid if bHasOldMove is true

private:

	FPhysicNetworkMoveData BaseDefaultMoveData[3];
};


//////////////////////////////////////////////////////////////////////////
/**
 * Structure used internally to handle serialization of FPhysicNetworkMoveDataContainer over the network.
 */
USTRUCT()
struct PHYSICSREPLICATION_API FPhysicServerMovePackedBits : public FPhysicNetworkSerializationPackedBits
{
	GENERATED_BODY()
	FPhysicServerMovePackedBits() {}
};

template<>
struct TStructOpsTypeTraits<FPhysicServerMovePackedBits> : public TStructOpsTypeTraitsBase2<FPhysicServerMovePackedBits>
{
	enum
	{
		WithNetSerializer = true,
	};
};


//////////////////////////////////////////////////////////////////////////
// Server to Client response
//////////////////////////////////////////////////////////////////////////

// ClientAdjustPosition replication (event called at end of frame by server)
struct PHYSICSREPLICATION_API FClientAdjustmentPhysic
{
public:

	FClientAdjustmentPhysic()
		: TimeStamp(0.f)
		, DeltaTime(0.f)
		, NewLoc(ForceInitToZero)
		, NewVel(ForceInitToZero)
		, NewRot(ForceInitToZero)
		, NewBase(nullptr)
		, NewBaseBoneName(NAME_None)
		, bAckGoodMove(false)
		, bBaseRelativePosition(false)
		, MovementMode(0)
	{
	}

	float TimeStamp;
	float DeltaTime;
	FVector NewLoc;
	FVector NewVel;
	FRotator NewRot;
	UPrimitiveComponent* NewBase;
	FName NewBaseBoneName;
	bool bAckGoodMove;
	bool bBaseRelativePosition;
	uint8 MovementMode;
};


//////////////////////////////////////////////////////////////////////////
/**
 * Response from the server to the client about a move that is being acknowledged.
 * Internally it mainly copies the FClientAdjustmentPhysic from the UCharacterMovementComponent indicating the response, as well as
 * setting a few relevant flags about the response and serializing the response to and from an FArchive for handling the variable-size
 * payload over the network.
 */
struct PHYSICSREPLICATION_API FPhysicMoveResponseDataContainer
{
public:

	FPhysicMoveResponseDataContainer()
		: bHasBase(false)
		, bHasRotation(false)
	{
	}

	virtual ~FPhysicMoveResponseDataContainer()
	{
	}

	/**
	 * Copy the FClientAdjustmentPhysic and set a few flags relevant to that data.
	 */
	virtual void ServerFillResponseData(const UPhysicsMovementComponent& CharacterMovement, const FClientAdjustmentPhysic& PendingAdjustment);

	/**
	 * Serialize the FClientAdjustmentPhysic data and other internal flags.
	 */
	virtual bool Serialize(UPhysicsMovementComponent& CharacterMovement, FArchive& Ar, UPackageMap* PackageMap);

	bool IsGoodMove() const		{ return ClientAdjustment.bAckGoodMove;}
	bool IsCorrection() const	{ return !IsGoodMove(); }

	bool bHasBase;
	bool bHasRotation; // By default ClientAdjustment.NewRot is not serialized. Set this to true after base ServerFillResponseData if you want Rotation to be serialized.

	// Client adjustment. All data other than bAckGoodMove and TimeStamp is only valid if this is a correction (not an ack).
	FClientAdjustmentPhysic ClientAdjustment;

};

//////////////////////////////////////////////////////////////////////////
/**
 * Structure used internally to handle serialization of FPhysicMoveResponseDataContainer over the network.
 */
USTRUCT()
struct PHYSICSREPLICATION_API FPhysicMoveResponsePackedBits : public FPhysicNetworkSerializationPackedBits
{
	GENERATED_BODY()
	FPhysicMoveResponsePackedBits() {}
};

template<>
struct TStructOpsTypeTraits<FPhysicMoveResponsePackedBits> : public TStructOpsTypeTraitsBase2<FPhysicMoveResponsePackedBits>
{
	enum
	{
		WithNetSerializer = true,
	};
};
