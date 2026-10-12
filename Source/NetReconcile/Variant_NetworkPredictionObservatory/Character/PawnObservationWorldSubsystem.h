#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "UObject/ObjectKey.h"
#include "Model/PawnObservationSnapshot.h"
#include "PawnObservationWorldSubsystem.generated.h"

DECLARE_MULTICAST_DELEGATE_OneParam(FOnPawnObservationCaptured, const FPawnObservationSnapshot&);

struct FPawnObservationSubscription
{
	FObjectKey PawnKey;
	FDelegateHandle Delegate;
	inline bool IsValid() const { return Delegate.IsValid(); }
};

/** Samples each interested Pawn once per normal, unpaused game frame, shared by all consumers. */
UCLASS()
class NETRECONCILE_API UPawnObservationWorldSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	virtual void OnWorldEndPlay(UWorld& World) override;

	FPawnObservationSubscription Subscribe(APawn& Pawn, FOnPawnObservationCaptured::FDelegate Listener);
	void Unsubscribe(const FPawnObservationSubscription& Subscription);
	FPawnObservationSnapshot GetLatest(const APawn& Pawn) const;

protected:
	virtual bool DoesSupportWorldType(EWorldType::Type Type) const override;

private:
	struct FEntry
	{
		TWeakObjectPtr<APawn> Pawn;
		TWeakObjectPtr<AController> Controller;
		TWeakObjectPtr<class UObservationCharacterMovementComponent> MovementComponent;
		ENetRole Role = ROLE_None;
		FGuid SourceEpoch;
		FGuid MovementObservationContextEpoch;
		FPawnObservationSnapshot Snapshot;
		TSharedPtr<FOnPawnObservationCaptured> Listeners = MakeShared<FOnPawnObservationCaptured>();
	};

	TMap<FObjectKey, FEntry> Entries;
	int64 PublicationSequence = 0;

	FDelegateHandle PostActorTickHandle;

	void CaptureWorld(UWorld* World, ELevelTick Type, float DeltaTime);
	static bool IsCurrent(const FEntry& Entry, const APawn& Pawn);
	void Clear();
};
