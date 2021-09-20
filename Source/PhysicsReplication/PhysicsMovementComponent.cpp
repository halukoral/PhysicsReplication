// Fill out your copyright notice in the Description page of Project Settings.


#include "PhysicsMovementComponent.h"

#include "PhysicsReplicationCharacter.h"
#include "Net/UnrealNetwork.h"

void FSavedMove_Physics::Clear()
{
	TimeStamp = 0.f;
	DeltaTime = 0.f;
	CustomTimeDilation = 1.0f;

	StartPackedMovementMode = 0;
	StartLocation = FVector::ZeroVector;
	StartRelativeLocation = FVector::ZeroVector;
	StartVelocity = FVector::ZeroVector;
	StartRotation = FRotator::ZeroRotator;
	StartControlRotation = FRotator::ZeroRotator;
	StartBaseRotation = FQuat::Identity;
	StartBase = nullptr;

	SavedLocation = FVector::ZeroVector;
	SavedRotation = FRotator::ZeroRotator;
	SavedRelativeLocation = FVector::ZeroVector;
	SavedControlRotation = FRotator::ZeroRotator;
	Acceleration = FVector::ZeroVector;
	MaxSpeed = 0.0f;
	AccelMag = 0.0f;
	AccelNormal = FVector::ZeroVector;

	EndBase = nullptr;
	EndPackedMovementMode = 0;

}

void FSavedMove_Physics::SetMoveFor(ACharacter* C, float InDeltaTime, FVector const& NewAccel, FNetworkPredictionData_Client_Physics& ClientData)
{
	CharacterOwner = Character;
	DeltaTime = InDeltaTime;
	
	SetInitialPosition(Character);

	AccelMag = NewAccel.Size();
	AccelNormal = (AccelMag > SMALL_NUMBER ? NewAccel / AccelMag : FVector::ZeroVector);
	
	// Round value, so that client and server match exactly (and so we can send with less bandwidth). This rounded value is copied back to the client in ReplicateMoveToServer.
	// This is done after the AccelMag and AccelNormal are computed above, because those are only used client-side for combining move logic and need to remain accurate.
	Acceleration = Character->GetCharacterMovement()->RoundAcceleration(NewAccel);
	
	MaxSpeed = Character->GetCharacterMovement()->GetMaxSpeed();

	JumpCurrentCount = Character->JumpCurrentCountPreJump;
	bWantsToCrouch = Character->GetCharacterMovement()->bWantsToCrouch;
	bForceMaxAccel = Character->GetCharacterMovement()->bForceMaxAccel;
	StartPackedMovementMode = Character->GetCharacterMovement()->PackNetworkMovementMode();
	MovementMode = StartPackedMovementMode; // Deprecated, keep backwards compat until removed

	// Root motion source-containing moves should never be combined
	// Main discovered issue being a move without root motion combining with
	// a move with it will cause the DeltaTime for that next move to be larger than
	// intended (effectively root motion applies to movement that happened prior to its activation)
	if (Character->GetCharacterMovement()->CurrentRootMotion.HasActiveRootMotionSources())
	{
		bForceNoCombine = true;
	}

	// Moves with anim root motion should not be combined
	const FAnimMontageInstance* RootMotionMontageInstance = Character->GetRootMotionAnimMontageInstance();
	if (RootMotionMontageInstance)
	{
		bForceNoCombine = true;
	}

	// Launch velocity gives instant and potentially huge change of velocity
	// Avoid combining move to prevent from reverting locations until server catches up
	const bool bHasLaunchVelocity = !Character->GetCharacterMovement()->PendingLaunchVelocity.IsZero();
	if (bHasLaunchVelocity)
	{
		bForceNoCombine = true;
	}

	TimeStamp = ClientData.CurrentTimeStamp;
}

void FSavedMove_Physics::SetInitialPosition(ACharacter* C)
{
	StartLocation = Character->GetActorLocation();
	StartRotation = Character->GetActorRotation();
	StartVelocity = Character->GetCharacterMovement()->Velocity;
	UPrimitiveComponent* const MovementBase = Character->GetMovementBase();
	StartBase = MovementBase;
	StartBaseRotation = FQuat::Identity;
	StartFloor = Character->GetCharacterMovement()->CurrentFloor;
	CustomTimeDilation = Character->CustomTimeDilation;
	StartBoneName = Character->GetBasedMovement().BoneName;
	StartActorOverlapCounter = Character->NumActorOverlapEventsCounter;
	StartComponentOverlapCounter = UPrimitiveComponent::GlobalOverlapEventsCounter;

	if (MovementBaseUtility::UseRelativeLocation(MovementBase))
	{
		StartRelativeLocation = Character->GetBasedMovement().Location;
		FVector StartBaseLocation_Unused;
		MovementBaseUtility::GetMovementBaseTransform(MovementBase, StartBoneName, StartBaseLocation_Unused, StartBaseRotation);
	}

	// Attachment state
	if (const USceneComponent* UpdatedComponent = Character->GetCharacterMovement()->UpdatedComponent)
	{
		StartAttachParent = UpdatedComponent->GetAttachParent();
		StartAttachSocketName = UpdatedComponent->GetAttachSocketName();
		StartAttachRelativeLocation = UpdatedComponent->GetRelativeLocation();
		StartAttachRelativeRotation = UpdatedComponent->GetRelativeRotation();
	}

	StartControlRotation = Character->GetControlRotation().Clamp();
	Character->GetCapsuleComponent()->GetScaledCapsuleSize(StartCapsuleRadius, StartCapsuleHalfHeight);

	// Jump state
	bPressedJump = Character->bPressedJump;
	bWasJumping = Character->bWasJumping;
	JumpKeyHoldTime = Character->JumpKeyHoldTime;
	JumpForceTimeRemaining = Character->JumpForceTimeRemaining;
	JumpMaxCount = Character->JumpMaxCount;
}

bool FSavedMove_Physics::IsImportantMove(const FSavedPhysicsMovePtr& LastAckedMove) const
{
	const FSavedMove_Character* LastAckedMove = LastAckedMovePtr.Get();

	// Check if any important movement flags have changed status.
	if (GetCompressedFlags() != LastAckedMove->GetCompressedFlags())
	{
		return true;
	}

	if (StartPackedMovementMode != LastAckedMove->EndPackedMovementMode)
	{
		return true;
	}

	if (EndPackedMovementMode != LastAckedMove->EndPackedMovementMode)
	{
		return true;
	}

	// check if acceleration has changed significantly
	if (Acceleration != LastAckedMove->Acceleration)
	{
		// Compare magnitude and orientation
		if( (FMath::Abs(AccelMag - LastAckedMove->AccelMag) > AccelMagThreshold) || ((AccelNormal | LastAckedMove->AccelNormal) < AccelDotThreshold) )
		{
			return true;
		}
	}
	return false;}

FVector FSavedMove_Physics::GetRevertedLocation() const
{
	if (const USceneComponent* AttachParent = StartAttachParent.Get())
	{
		return AttachParent->GetSocketTransform(StartAttachSocketName).TransformPosition(StartAttachRelativeLocation);
	}

	const UPrimitiveComponent* MovementBase = StartBase.Get();
	if (MovementBaseUtility::UseRelativeLocation(MovementBase))
	{
		FVector BaseLocation; FQuat BaseRotation;
		MovementBaseUtility::GetMovementBaseTransform(MovementBase, StartBoneName, BaseLocation, BaseRotation);
		return BaseLocation + StartRelativeLocation;
	}

	return StartLocation;}

void FSavedMove_Physics::PostUpdate(ACharacter* C, EPostUpdateMode PostUpdateMode)
{
		// Common code for both recording and after a replay.
	{
		EndPackedMovementMode = Character->GetCharacterMovement()->PackNetworkMovementMode();
		MovementMode = EndPackedMovementMode; // Deprecated, keep backwards compat until removed
		SavedLocation = Character->GetActorLocation();
		SavedRotation = Character->GetActorRotation();
		SavedVelocity = Character->GetVelocity();
#if ENABLE_NAN_DIAGNOSTIC
		const float WarnVelocitySqr = 20000.f * 20000.f;
		if (SavedVelocity.SizeSquared() > WarnVelocitySqr)
		{
			if (Character->SavedRootMotion.HasActiveRootMotionSources())
			{
				UE_LOG(LogCharacterMovement, Log, TEXT("FSavedMove_Character::PostUpdate detected very high Velocity! (%s), but with active root motion sources (could be intentional)"), *SavedVelocity.ToString());
			}
			else
			{
				UE_LOG(LogCharacterMovement, Warning, TEXT("FSavedMove_Character::PostUpdate detected very high Velocity! (%s)"), *SavedVelocity.ToString());
			}
		}
#endif
		UPrimitiveComponent* const MovementBase = Character->GetMovementBase();
		EndBase = MovementBase;
		EndBoneName = Character->GetBasedMovement().BoneName;
		if (MovementBaseUtility::UseRelativeLocation(MovementBase))
		{
			SavedRelativeLocation = Character->GetBasedMovement().Location;
		}

		// Attachment state
		if (const USceneComponent* UpdatedComponent = Character->GetCharacterMovement()->UpdatedComponent)
		{
			EndAttachParent = UpdatedComponent->GetAttachParent();
			EndAttachSocketName = UpdatedComponent->GetAttachSocketName();
			EndAttachRelativeLocation = UpdatedComponent->GetRelativeLocation();
			EndAttachRelativeRotation = UpdatedComponent->GetRelativeRotation();
		}

		SavedControlRotation = Character->GetControlRotation().Clamp();
	}

	// Only save RootMotion params when initially recording
	if (PostUpdateMode == PostUpdate_Record)
	{
		const FAnimMontageInstance* RootMotionMontageInstance = Character->GetRootMotionAnimMontageInstance();
		if (RootMotionMontageInstance)
		{
			if (!RootMotionMontageInstance->IsRootMotionDisabled())
			{
				RootMotionMontage = RootMotionMontageInstance->Montage;
				RootMotionTrackPosition = RootMotionMontageInstance->GetPosition();
				RootMotionMovement = Character->ClientRootMotionParams;
			}

			// Moves where anim root motion is being played should not be combined
			bForceNoCombine = true;
		}

		// Save off Root Motion Sources
		if( Character->SavedRootMotion.HasActiveRootMotionSources() )
		{
			SavedRootMotion = Character->SavedRootMotion;
			bForceNoCombine = true;
		}

		// Don't want to combine moves that trigger overlaps, because by moving back and replaying the move we could retrigger overlaps.
		EndActorOverlapCounter = Character->NumActorOverlapEventsCounter;
		EndComponentOverlapCounter = UPrimitiveComponent::GlobalOverlapEventsCounter;
		if ((StartActorOverlapCounter != EndActorOverlapCounter) || (StartComponentOverlapCounter != EndComponentOverlapCounter))
		{
			bForceNoCombine = true;
		}

		// Don't combine or delay moves where velocity changes to/from zero.
		if (StartVelocity.IsZero() != SavedVelocity.IsZero())
		{
			bForceNoCombine = true;
		}

		// Don't combine if this move caused us to change movement modes during the move.
		if (StartPackedMovementMode != EndPackedMovementMode)
		{
			bForceNoCombine = true;
		}

		// Don't combine when jump input just began or ended during the move.
		if (bPressedJump != CharacterOwner->bPressedJump)
		{
			bForceNoCombine = true;
		}
	}
	else if (PostUpdateMode == PostUpdate_Replay)
	{
		if( Character->bClientResimulateRootMotionSources )
		{
			// When replaying moves, the next move should use the results of this move
			// so that future replayed moves account for the server correction
			Character->SavedRootMotion = Character->GetCharacterMovement()->CurrentRootMotion;
		}
	}
}

bool FSavedMove_Physics::CanCombineWith(const FSavedPhysicsMovePtr& NewMove, ACharacter* InCharacter, float MaxDelta) const
{
	const FSavedMove_Character* NewMove = NewMovePtr.Get();

	if (bForceNoCombine || NewMove->bForceNoCombine)
	{
		return false;
	}

	if (bOldTimeStampBeforeReset)
	{
		return false;
	}

	// Cannot combine moves which contain root motion for now.
	// @fixme laurent - we should be able to combine most of them though, but current scheme of resetting pawn location and resimulating forward doesn't work.
	// as we don't want to tick montage twice (so we don't fire events twice). So we need to rearchitecture this so we tick only the second part of the move, and reuse the first part.
	if( (RootMotionMontage != NULL) || (NewMove->RootMotionMontage != NULL) )
	{
		return false;
	}

	if (NewMove->Acceleration.IsZero())
	{
		if (!Acceleration.IsZero())
		{
			return false;
		}
	}
	else
	{
		if (NewMove->DeltaTime + DeltaTime >= MaxDelta)
		{
			return false;
		}

		if (!FVector::Coincident(AccelNormal, NewMove->AccelNormal, AccelDotThresholdCombine))
		{
			return false;
		}	
	}

	// Don't combine moves where velocity changes to zero or from zero.
	if (StartVelocity.IsZero() != NewMove->StartVelocity.IsZero())
	{
		return false;
	}

	if (!FMath::IsNearlyEqual(MaxSpeed, NewMove->MaxSpeed, MaxSpeedThresholdCombine))
	{
		return false;
	}

	if ((MaxSpeed == 0.0f) != (NewMove->MaxSpeed == 0.0f))
	{
		return false;
	}

	// Don't combine on changes to/from zero JumpKeyHoldTime.
	if ((JumpKeyHoldTime == 0.f) != (NewMove->JumpKeyHoldTime == 0.f))
	{
		return false;
	}

	if ((bWasJumping != NewMove->bWasJumping) || (JumpCurrentCount != NewMove->JumpCurrentCount) || (JumpMaxCount != NewMove->JumpMaxCount))
	{
		return false;
	}
	
	// Don't combine on changes to/from zero.
	if ((JumpForceTimeRemaining == 0.f) != (NewMove->JumpForceTimeRemaining == 0.f))
	{
		return false;
	}
	
	// Compressed flags not equal, can't combine. This covers jump and crouch as well as any custom movement flags from overrides.
	if (GetCompressedFlags() != NewMove->GetCompressedFlags())
	{
		return false;
	}

	const UPrimitiveComponent* OldBasePtr = StartBase.Get();
	const UPrimitiveComponent* NewBasePtr = NewMove->StartBase.Get();
	const bool bDynamicBaseOld = MovementBaseUtility::IsDynamicBase(OldBasePtr);
	const bool bDynamicBaseNew = MovementBaseUtility::IsDynamicBase(NewBasePtr);

	// Change between static/dynamic requires separate moves (position sent as world vs relative)
	if (bDynamicBaseOld != bDynamicBaseNew)
	{
		return false;
	}

	// Only need to prevent combining when on a dynamic base that changes (unless forced off via CVar). Again, because relative location can change.
	const bool bPreventOnStaticBaseChange = (CharacterMovementCVars::NetEnableMoveCombiningOnStaticBaseChange == 0);
	if (bPreventOnStaticBaseChange || (bDynamicBaseOld || bDynamicBaseNew))
	{
		if (OldBasePtr != NewBasePtr)
		{
			return false;
		}

		if (StartBoneName != NewMove->StartBoneName)
		{
			return false;
		}
	}

	if (StartPackedMovementMode != NewMove->StartPackedMovementMode)
	{
		return false;
	}

	if (EndPackedMovementMode != NewMove->StartPackedMovementMode)
	{
		return false;
	}

	if (StartCapsuleRadius != NewMove->StartCapsuleRadius)
	{
		return false;
	}

	if (StartCapsuleHalfHeight != NewMove->StartCapsuleHalfHeight)
	{
		return false;
	}

	// No combining if attach parent changed.
	const USceneComponent* OldStartAttachParent = StartAttachParent.Get();
	const USceneComponent* OldEndAttachParent = EndAttachParent.Get();
	const USceneComponent* NewStartAttachParent = NewMove->StartAttachParent.Get();
	if (OldStartAttachParent != NewStartAttachParent || OldEndAttachParent != NewStartAttachParent)
	{
		return false;
	}

	// No combining if attach socket changed.
	if (StartAttachSocketName != NewMove->StartAttachSocketName || EndAttachSocketName != NewMove->StartAttachSocketName)
	{
		return false;
	}

	if (NewStartAttachParent != nullptr)
	{
		// If attached, no combining if relative location changed.
		const FVector RelativeLocationDelta = (StartAttachRelativeLocation - NewMove->StartAttachRelativeLocation);
		if (!RelativeLocationDelta.IsNearlyZero(CharacterMovementCVars::NetMoveCombiningAttachedLocationTolerance))
		{
			//UE_LOG(LogCharacterMovement, Warning, TEXT("NoCombine: DeltaLocation(%s)"), *RelativeLocationDelta.ToString());
			return false;
		}
		// For rotation, Yaw doesn't matter for capsules
		FRotator RelativeRotationDelta = StartAttachRelativeRotation - NewMove->StartAttachRelativeRotation;
		RelativeRotationDelta.Yaw = 0.0f;
		if (!RelativeRotationDelta.IsNearlyZero(CharacterMovementCVars::NetMoveCombiningAttachedRotationTolerance))
		{
			return false;
		}
	}
	else
	{
		// Not attached to anything. Only combine if base hasn't rotated.
		if (!StartBaseRotation.Equals(NewMove->StartBaseRotation))
		{
			return false;
		}
	}

	if (CustomTimeDilation != NewMove->CustomTimeDilation)
	{
		return false;
	}

	// Don't combine moves with overlap event changes, since reverting back and then moving forward again can cause event spam.
	// This catches events between movement updates; moves that trigger events already set bForceNoCombine to false.
	if (EndActorOverlapCounter != NewMove->StartActorOverlapCounter)
	{
		return false;
	}

	return true;}

void FSavedMove_Physics::CombineWith(const FSavedMove_Physics* OldMove, ACharacter* InCharacter, APlayerController* PC,	const FVector& OldStartLocation)
{
	UCharacterMovementComponent* CharMovement = InCharacter->GetCharacterMovement();

	// to combine move, first revert pawn position to PendingMove start position, before playing combined move on client
	if (const USceneComponent* AttachParent = StartAttachParent.Get())
	{
		CharMovement->UpdatedComponent->SetRelativeLocationAndRotation(StartAttachRelativeLocation, StartAttachRelativeRotation, false, nullptr, CharMovement->GetTeleportType());
	}
	else
	{
		CharMovement->UpdatedComponent->SetWorldLocationAndRotation(OldStartLocation, OldMove->StartRotation, false, nullptr, CharMovement->GetTeleportType());
	}
	
	CharMovement->Velocity = OldMove->StartVelocity;

	CharMovement->SetBase(OldMove->StartBase.Get(), OldMove->StartBoneName);
	CharMovement->CurrentFloor = OldMove->StartFloor;

	// Now that we have reverted to the old position, prepare a new move from that position,
	// using our current velocity, acceleration, and rotation, but applied over the combined time from the old and new move.

	// Combine times for both moves
	DeltaTime += OldMove->DeltaTime;

	// Roll back jump force counters. SetInitialPosition() below will copy them to the saved move.
	InCharacter->JumpForceTimeRemaining = OldMove->JumpForceTimeRemaining;
	InCharacter->JumpKeyHoldTime = OldMove->JumpKeyHoldTime;
	InCharacter->JumpCurrentCountPreJump = OldMove->JumpCurrentCount;
}

void FSavedMove_Physics::PrepMoveFor(ACharacter* C)
{
		if( RootMotionMontage != NULL )
	{
		// If we need to resimulate Root Motion, then do so.
		if( Character->bClientResimulateRootMotion )
		{
			// Make sure RootMotion montage matches what we are playing now.
			FAnimMontageInstance * RootMotionMontageInstance = Character->GetRootMotionAnimMontageInstance();
			if( RootMotionMontageInstance && (RootMotionMontage == RootMotionMontageInstance->Montage) )
			{
				RootMotionMovement.Clear();
				RootMotionTrackPosition = RootMotionMontageInstance->GetPosition();
				RootMotionMontageInstance->SimulateAdvance(DeltaTime, RootMotionTrackPosition, RootMotionMovement);
				RootMotionMontageInstance->SetPosition(RootMotionTrackPosition);
				RootMotionMovement.ScaleRootMotionTranslation(Character->GetAnimRootMotionTranslationScale());
			}
		}

		// Restore root motion to that of this SavedMove to be used during replaying the Move
		Character->GetCharacterMovement()->RootMotionParams = RootMotionMovement;
	}

	// Resimulate Root Motion Sources if we need to - occurs after server RPCs over a correction during root motion sources.
	if( SavedRootMotion.HasActiveRootMotionSources() || Character->SavedRootMotion.HasActiveRootMotionSources() )
	{
		if( Character->bClientResimulateRootMotionSources )
		{
			// Note: This may need to change to a SimulatePrepare() that doesn't depend on everything
			// being "currently active" - if we have sources that are no longer around or valid,
			// we're not able to properly re-prepare them, and should just keep whatever we currently have

			// Apply any corrections/state from either last played move or last received from server (in ACharacter::SavedRootMotion)
			UE_LOG(LogRootMotion, VeryVerbose, TEXT("SavedMove SavedRootMotion getting updated for SavedMove replays: %s"), *Character->GetName());
			SavedRootMotion.UpdateStateFrom(Character->SavedRootMotion);
			SavedRootMotion.CleanUpInvalidRootMotion(DeltaTime, *Character, *Character->GetCharacterMovement());
			SavedRootMotion.PrepareRootMotion(DeltaTime, *Character, *Character->GetCharacterMovement());
		}
		else
		{
			// If this is not the first SavedMove we are replaying, clean up any currently applied root motion sources so that if they have
			// SetVelocity/ClampVelocity finish settings they get applied correctly before CurrentRootMotion gets stomped below
			if (FNetworkPredictionData_Client_Physics* ClientData = Character->GetCharacterMovement()->GetPredictionData_Client_Character())
			{
				if (ClientData->SavedMoves[0].Get() != this)
				{
					Character->GetCharacterMovement()->CurrentRootMotion.CleanUpInvalidRootMotion(DeltaTime, *Character, *Character->GetCharacterMovement());
				}
			}
		}

		// Restore root motion to that of this SavedMove to be used during replaying the Move
		Character->GetCharacterMovement()->CurrentRootMotion = SavedRootMotion;
	}

	Character->GetCharacterMovement()->bForceMaxAccel = bForceMaxAccel;
	Character->bWasJumping = bWasJumping;
	Character->JumpKeyHoldTime = JumpKeyHoldTime;
	Character->JumpForceTimeRemaining = JumpForceTimeRemaining;
	Character->JumpMaxCount = JumpMaxCount;
	Character->JumpCurrentCount = JumpCurrentCount;
	Character->JumpCurrentCountPreJump = JumpCurrentCount;

	StartPackedMovementMode = Character->GetCharacterMovement()->PackNetworkMovementMode();
}

bool FSavedMove_Physics::IsMatchingStartControlRotation(const APlayerController* PC) const
{
	return PC ? StartControlRotation.Equals(PC->GetControlRotation(), CharacterMovementCVars::NetStationaryRotationTolerance) : false;
}

void FSavedMove_Physics::GetPackedAngles(uint32& YawAndPitchPack, uint8& RollPack) const
{
	// Compress rotation down to 5 bytes
	YawAndPitchPack = UPhysicsMovementComponent::PackYawAndPitchTo32(SavedControlRotation.Yaw, SavedControlRotation.Pitch);
	RollPack = FRotator::CompressAxisToByte(SavedControlRotation.Roll);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
UPhysicsMovementComponent::UPhysicsMovementComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	SetIsReplicated(true);
}

void UPhysicsMovementComponent::CallServerMovePacked(const FSavedMove_Physics* NewMove,
	const FSavedMove_Physics* PendingMove, const FSavedMove_Physics* OldMove)
{
}

void UPhysicsMovementComponent::CombineWith(const FSavedMove_Physics* OldMove, const FVector& OldStartLocation)
{
}

bool UPhysicsMovementComponent::CanDelaySendingMove(const FSavedPhysicsMovePtr& NewMove)
{
	return true;
}

float UPhysicsMovementComponent::GetClientNetSendDeltaTime(const FNetworkPredictionData_Client_Physics* ClientData,
	const FSavedPhysicsMovePtr& NewMove) const
{
	return 0;
}

bool FPhysicNetworkSerializationPackedBits::NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess)
{
	const bool bLocalSuccess = true;
	SavedPackageMap = Map;

	// Array size in bits, using minimal number of bytes to write it out.
	uint32 NumBits = DataBits.Num();
	Ar.SerializeIntPacked(NumBits);

	if (Ar.IsLoading())
	{
		DataBits.Init(0, NumBits);
	}

	// Array data
	Ar.SerializeBits(DataBits.GetData(), NumBits);

	bOutSuccess = bLocalSuccess;
	return !Ar.IsError();
}

void FPhysicNetworkMoveData::ClientFillNetworkMoveData(const FSavedMove_Physics& ClientMove, ENetworkMoveType MoveType)
{
	NetworkMoveType = MoveType;

	TimeStamp = ClientMove.TimeStamp;
	Acceleration = ClientMove.Acceleration;
	ControlRotation = ClientMove.SavedControlRotation;

	// Location, relative movement base, and ending movement mode is only used for error checking, so only fill in the more complex parts if actually required.
	if (MoveType == ENetworkMoveType::NewMove)
	{
		// Determine if we send absolute or relative location
		UPrimitiveComponent* ClientMovementBase = ClientMove.EndBase.Get();
		const bool bDynamicBase = MovementBaseUtility::UseRelativeLocation(ClientMovementBase);
		const FVector SendLocation = bDynamicBase ? ClientMove.SavedRelativeLocation : ClientMove.SavedLocation;

		Location = SendLocation;
		MovementBase = bDynamicBase ? ClientMovementBase : nullptr;
	}
	else
	{
		Location = ClientMove.SavedLocation;
		MovementBase = nullptr;
	}
}

bool FPhysicNetworkMoveData::Serialize(UPhysicsMovementComponent& PhysicsMovement, FArchive& Ar, UPackageMap* PackageMap, ENetworkMoveType MoveType)
{
	NetworkMoveType = MoveType;

	bool bLocalSuccess = true;
	const bool bIsSaving = Ar.IsSaving();

	Ar << TimeStamp;

	// TODO: better packing with single bit per component indicating zero/non-zero
	Acceleration.NetSerialize(Ar, PackageMap, bLocalSuccess);

	Location.NetSerialize(Ar, PackageMap, bLocalSuccess);

	// ControlRotation : FRotator handles each component zero/non-zero test; it uses a single signal bit for zero/non-zero, and uses 16 bits per component if non-zero.
	ControlRotation.NetSerialize(Ar, PackageMap, bLocalSuccess);

	SerializeOptionalValue<uint8>(bIsSaving, Ar, CompressedMoveFlags, 0);

	if (MoveType == ENetworkMoveType::NewMove)
	{
		// Location, relative movement base, and ending movement mode is only used for error checking, so only save for the final move.
		SerializeOptionalValue<UPrimitiveComponent*>(bIsSaving, Ar, MovementBase, nullptr);
		SerializeOptionalValue<uint8>(bIsSaving, Ar, MovementMode, MOVE_Walking);
	}

	return !Ar.IsError();
}

void FPhysicNetworkMoveDataContainer::ClientFillNetworkMoveData(const FSavedMove_Physics* ClientNewMove,
	const FSavedMove_Physics* ClientPendingMove, const FSavedMove_Physics* ClientOldMove)
{
	bDisableCombinedScopedMove = false;

	if (ensure(ClientNewMove))
	{
		NewMoveData->ClientFillNetworkMoveData(*ClientNewMove, FPhysicNetworkMoveData::ENetworkMoveType::NewMove);
	}

	bHasPendingMove = (ClientPendingMove != nullptr);
	if (bHasPendingMove)
	{
		PendingMoveData->ClientFillNetworkMoveData(*ClientPendingMove, FPhysicNetworkMoveData::ENetworkMoveType::PendingMove);
	}
	else
	{
		bIsDualHybridRootMotionMove = false;
	}
	
	bHasOldMove = (ClientOldMove != nullptr);
	if (bHasOldMove)
	{
		OldMoveData->ClientFillNetworkMoveData(*ClientOldMove, FPhysicNetworkMoveData::ENetworkMoveType::OldMove);
	}
}

bool FPhysicNetworkMoveDataContainer::Serialize(UPhysicsMovementComponent& CharacterMovement, FArchive& Ar,
	UPackageMap* PackageMap)
{
	// We must have data storage initialized. If not, then the storage container wasn't properly initialized.
	check(NewMoveData && PendingMoveData && OldMoveData);

	// Base move always serialized.
	if (!NewMoveData->Serialize(CharacterMovement, Ar, PackageMap, FPhysicNetworkMoveData::ENetworkMoveType::NewMove))
	{
		return false;
	}
		
	// Optional pending dual move
	Ar.SerializeBits(&bHasPendingMove, 1);
	if (bHasPendingMove)
	{
		Ar.SerializeBits(&bIsDualHybridRootMotionMove, 1);
		if (!PendingMoveData->Serialize(CharacterMovement, Ar, PackageMap, FPhysicNetworkMoveData::ENetworkMoveType::PendingMove))
		{
			return false;
		}
	}

	// Optional old move
	Ar.SerializeBits(&bHasOldMove, 1);
	if (bHasOldMove)
	{
		if (!OldMoveData->Serialize(CharacterMovement, Ar, PackageMap, FPhysicNetworkMoveData::ENetworkMoveType::OldMove))
		{
			return false;
		}
	}

	Ar.SerializeBits(&bDisableCombinedScopedMove, 1);

	return !Ar.IsError();
}

void FPhysicMoveResponseDataContainer::ServerFillResponseData(const UPhysicsMovementComponent& CharacterMovement,
	const FClientAdjustmentPhysic& PendingAdjustment)
{
	bHasBase = false;
	bHasRotation = false;

	ClientAdjustment = PendingAdjustment;

	if (!PendingAdjustment.bAckGoodMove)
	{
		bHasBase = (PendingAdjustment.NewBase != nullptr);
	}

}

bool FPhysicMoveResponseDataContainer::Serialize(UPhysicsMovementComponent& CharacterMovement, FArchive& Ar,
	UPackageMap* PackageMap)
{
	bool bLocalSuccess = true;
	const bool bIsSaving = Ar.IsSaving();

	Ar.SerializeBits(&ClientAdjustment.bAckGoodMove, 1);
	Ar << ClientAdjustment.TimeStamp;

	if (IsCorrection())
	{
		Ar.SerializeBits(&bHasBase, 1);
		Ar.SerializeBits(&bHasRotation, 1);

		ClientAdjustment.NewLoc.NetSerialize(Ar, PackageMap, bLocalSuccess);
		ClientAdjustment.NewVel.NetSerialize(Ar, PackageMap, bLocalSuccess);

		if (bHasRotation)
		{
			ClientAdjustment.NewRot.NetSerialize(Ar, PackageMap, bLocalSuccess);
		}
		else if (!bIsSaving)
		{
			ClientAdjustment.NewRot = FRotator::ZeroRotator;
		}

		SerializeOptionalValue<UPrimitiveComponent*>(bIsSaving, Ar, ClientAdjustment.NewBase, nullptr);
		SerializeOptionalValue<FName>(bIsSaving, Ar, ClientAdjustment.NewBaseBoneName, NAME_None);
		SerializeOptionalValue<uint8>(bIsSaving, Ar, ClientAdjustment.MovementMode, MOVE_Walking);
		Ar.SerializeBits(&ClientAdjustment.bBaseRelativePosition, 1);
	}

	return !Ar.IsError();

}
