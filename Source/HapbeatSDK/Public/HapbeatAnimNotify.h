// Copyright (c) 2026 Hapbeat. MIT License.
#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "HapbeatAnimNotify.generated.h"

class UHapbeatEventMap;
class UHapbeatStreamPlayback;
class USkeletalMeshComponent;

/**
 * Fires a Hapbeat event at a point in an animation.
 *
 * This is the UE counterpart of Unity's HapbeatStateBehaviour, which attaches to
 * an Animator state and fires on state enter. UE has no per-state behaviour
 * object; the equivalent authoring surface -- and the one animators already
 * reach for -- is a notify placed on the animation timeline, which is strictly
 * more precise (an exact frame rather than "whenever this state begins").
 *
 * Use this for one-shot impacts (a footfall, a hit landing). For anything with a
 * duration, use UHapbeatAnimNotifyState below, which also stops on exit.
 */
UCLASS(Blueprintable, meta = (DisplayName = "Hapbeat Event"))
class HAPBEATSDK_API UHapbeatAnimNotify : public UAnimNotify
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hapbeat")
	TObjectPtr<UHapbeatEventMap> EventMap;

	/** Stable GUID of the entry to fire. Copy it from the Event Map window. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hapbeat")
	FGuid EntryId;

	/** Multiplies the entry's effective gain, so one entry can serve several animations. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hapbeat",
		meta = (UIMin = "0.0", UIMax = "2.0", ClampMin = "0.0", ClampMax = "2.0"))
	float GainMultiplier = 1.0f;

	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;
	virtual FString GetNotifyName_Implementation() const override;
};

/**
 * Starts a Hapbeat event when a window in an animation opens and stops it when
 * the window closes.
 *
 * Mirrors the half of Unity's HapbeatStateBehaviour that matters most: a looping
 * Stream Clip started on state enter is stopped again on state exit, so a
 * sustained effect can never outlive the animation that owns it.
 */
UCLASS(Blueprintable, meta = (DisplayName = "Hapbeat Event (Sustained)"))
class HAPBEATSDK_API UHapbeatAnimNotifyState : public UAnimNotifyState
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hapbeat")
	TObjectPtr<UHapbeatEventMap> EventMap;

	/** Stable GUID of the entry to fire. Copy it from the Event Map window. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hapbeat")
	FGuid EntryId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hapbeat",
		meta = (UIMin = "0.0", UIMax = "2.0", ClampMin = "0.0", ClampMax = "2.0"))
	float GainMultiplier = 1.0f;

	virtual void NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference) override;
	virtual void NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;
	virtual FString GetNotifyName_Implementation() const override;

private:
	// Notify assets are shared by every actor playing the animation, so the
	// handle to stop cannot live in a plain member -- it is per playing mesh.
	UPROPERTY(Transient)
	TMap<TWeakObjectPtr<USkeletalMeshComponent>, TWeakObjectPtr<UHapbeatStreamPlayback>> ActivePlaybacks;
};
