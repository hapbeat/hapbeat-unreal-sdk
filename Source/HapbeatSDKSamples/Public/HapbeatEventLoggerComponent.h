// Copyright (c) 2026 Hapbeat. MIT License.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "HapbeatEventLoggerComponent.generated.h"

/**
 * Diagnostic component that prints a tagged, time-stamped line whenever one of
 * its Log* methods is invoked.
 *
 * The problem it solves: when several interaction events could plausibly drive
 * a haptic (overlap begin, hit, click, grab, release...), picking the right one
 * means knowing the order they actually fire in, which is not obvious from
 * documentation. Wire every candidate event to the matching method here, press
 * Play, and read the order off the log.
 *
 * Port of Hapbeat.HapbeatEventLogger (Unity SDK). Unity's shortcut methods are
 * named after XR Interaction Toolkit events; the equivalents here are named
 * after the engine events a UE project actually binds. LogEvent() covers
 * anything not in the list. The SDK depends on no interaction framework -- this
 * component only prints strings.
 */
UCLASS(ClassGroup = (Hapbeat), meta = (BlueprintSpawnableComponent, DisplayName = "Hapbeat Event Logger (Diagnostic)"))
class HAPBEATSDKSAMPLES_API UHapbeatEventLoggerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UHapbeatEventLoggerComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hapbeat",
		meta = (Tooltip = "Label prefix for logged lines. Empty = use the owning actor's name."))
	FString Label;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hapbeat",
		meta = (Tooltip = "Include a time stamp. Useful for measuring the gap between two events."))
	bool bIncludeTimestamp = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hapbeat",
		meta = (Tooltip = "Also draw each line on screen, not just in the output log."))
	bool bAlsoDrawOnScreen = false;

	/** Log an arbitrary tag. Bind anything here and name the event in the tag. */
	UFUNCTION(BlueprintCallable, Category = "Hapbeat", meta = (DisplayName = "Log Event (Hapbeat)"))
	void LogEvent(const FString& Tag);

	// Shortcuts for events whose delegate signature can't pass a string.
	UFUNCTION(BlueprintCallable, Category = "Hapbeat", meta = (DisplayName = "Log Begin Overlap (Hapbeat)")) void LogBeginOverlap();
	UFUNCTION(BlueprintCallable, Category = "Hapbeat", meta = (DisplayName = "Log End Overlap (Hapbeat)")) void LogEndOverlap();
	UFUNCTION(BlueprintCallable, Category = "Hapbeat", meta = (DisplayName = "Log Hit (Hapbeat)")) void LogHit();
	UFUNCTION(BlueprintCallable, Category = "Hapbeat", meta = (DisplayName = "Log Clicked (Hapbeat)")) void LogClicked();
	UFUNCTION(BlueprintCallable, Category = "Hapbeat", meta = (DisplayName = "Log Released (Hapbeat)")) void LogReleased();
	UFUNCTION(BlueprintCallable, Category = "Hapbeat", meta = (DisplayName = "Log Begin Cursor Over (Hapbeat)")) void LogBeginCursorOver();
	UFUNCTION(BlueprintCallable, Category = "Hapbeat", meta = (DisplayName = "Log End Cursor Over (Hapbeat)")) void LogEndCursorOver();
	UFUNCTION(BlueprintCallable, Category = "Hapbeat", meta = (DisplayName = "Log Grabbed (Hapbeat)")) void LogGrabbed();
	UFUNCTION(BlueprintCallable, Category = "Hapbeat", meta = (DisplayName = "Log Dropped (Hapbeat)")) void LogDropped();
	UFUNCTION(BlueprintCallable, Category = "Hapbeat", meta = (DisplayName = "Log Activated (Hapbeat)")) void LogActivated();
	UFUNCTION(BlueprintCallable, Category = "Hapbeat", meta = (DisplayName = "Log Deactivated (Hapbeat)")) void LogDeactivated();

private:
	void Emit(const FString& Tag) const;
	FString ResolveLabel() const;
};
