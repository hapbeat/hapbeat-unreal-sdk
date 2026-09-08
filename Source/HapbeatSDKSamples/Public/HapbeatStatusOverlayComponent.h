// Copyright (c) 2026 Hapbeat. MIT License.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "HapbeatStatusOverlayComponent.generated.h"

/**
 * Drop-in debug HUD for the SDK's live state: one status line (connection,
 * address override, streaming flag) plus a short scrolling log of connect /
 * disconnect / pong / error / stream-transition events.
 *
 * Port of Hapbeat.HapbeatStatusOverlay (Unity SDK). Unity draws into two UI
 * Text elements the user wires in the Inspector; this draws through the
 * engine's on-screen debug message channel instead, so the component works the
 * moment it is added -- no widget blueprint to author, nothing to wire, and
 * nothing left behind in a shipping UI. Log() is public for the same reason it
 * is in Unity: callers can push their own lines onto the same buffer instead of
 * building a second one.
 */
UCLASS(ClassGroup = (Hapbeat), meta = (BlueprintSpawnableComponent, DisplayName = "Hapbeat Status Overlay"))
class HAPBEATSDKSAMPLES_API UHapbeatStatusOverlayComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UHapbeatStatusOverlayComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hapbeat",
		meta = (ClampMin = "1", Tooltip = "How many log lines stay on screen. Older lines are dropped."))
	int32 MaxLogLines = 8;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hapbeat",
		meta = (Tooltip = "Draw the overlay. Turn off to keep the component but hide it."))
	bool bShowOverlay = true;

	/** Append a one-line entry to the on-screen log. */
	UFUNCTION(BlueprintCallable, Category = "Hapbeat", meta = (DisplayName = "Log Status Message (Hapbeat)"))
	void Log(const FString& Message);

	/** Drop every log line. */
	UFUNCTION(BlueprintCallable, Category = "Hapbeat", meta = (DisplayName = "Clear Status Log (Hapbeat)"))
	void ClearLog();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	// Delegate handlers. The subsystem's delegates are dynamic, so these have to
	// be UFUNCTIONs rather than lambdas.
	UFUNCTION() void HandleConnected();
	UFUNCTION() void HandleDisconnected();
	UFUNCTION() void HandleError(const FString& Message);
	UFUNCTION() void HandlePong(const FString& Endpoint, int64 RttUs, const FString& DeviceName, const FString& Address, const FString& Firmware);

	/** Current status line, rebuilt every tick. */
	FString BuildStatusLine() const;

	TArray<FString> LogLines;

	/** Tracks IsStreaming() so start/stop transitions get logged without the caller doing it. */
	bool bWasStreaming = false;

	/**
	 * Base key for the engine's on-screen message slots. Each component instance
	 * claims a small contiguous block so several overlays can coexist (Unity
	 * allows multiple instances too) without overwriting each other's lines.
	 */
	uint64 DebugMessageKeyBase = 0;
};
