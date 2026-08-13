// Copyright (c) 2026 Hapbeat. MIT License.
#include "HapbeatEventLoggerComponent.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

DEFINE_LOG_CATEGORY_STATIC(LogHapbeatEvents, Log, All);

UHapbeatEventLoggerComponent::UHapbeatEventLoggerComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

FString UHapbeatEventLoggerComponent::ResolveLabel() const
{
	if (!Label.IsEmpty())
	{
		return Label;
	}
	const AActor* Owner = GetOwner();
	return Owner != nullptr ? Owner->GetActorNameOrLabel() : GetName();
}

void UHapbeatEventLoggerComponent::Emit(const FString& Tag) const
{
	FString TimeStamp;
	if (bIncludeTimestamp)
	{
		const UWorld* World = GetWorld();
		// Real (unscaled) time: the point is to measure the gap between events,
		// which a paused or time-dilated world would distort.
		TimeStamp = FString::Printf(TEXT("  @t=%.3f"), World != nullptr ? World->GetRealTimeSeconds() : 0.0f);
	}

	const FString Line = FString::Printf(TEXT("[EventLog] %s: %s%s"), *ResolveLabel(), *Tag, *TimeStamp);
	UE_LOG(LogHapbeatEvents, Log, TEXT("%s"), *Line);

	if (bAlsoDrawOnScreen && GEngine != nullptr)
	{
		// Key -1: every line stacks, which is the point when reading firing order.
		GEngine->AddOnScreenDebugMessage(-1, 4.0f, FColor::Silver, Line);
	}
}

void UHapbeatEventLoggerComponent::LogEvent(const FString& Tag) { Emit(Tag); }

void UHapbeatEventLoggerComponent::LogBeginOverlap()    { Emit(TEXT("beginOverlap")); }
void UHapbeatEventLoggerComponent::LogEndOverlap()      { Emit(TEXT("endOverlap")); }
void UHapbeatEventLoggerComponent::LogHit()             { Emit(TEXT("hit")); }
void UHapbeatEventLoggerComponent::LogClicked()         { Emit(TEXT("clicked")); }
void UHapbeatEventLoggerComponent::LogReleased()        { Emit(TEXT("released")); }
void UHapbeatEventLoggerComponent::LogBeginCursorOver() { Emit(TEXT("beginCursorOver")); }
void UHapbeatEventLoggerComponent::LogEndCursorOver()   { Emit(TEXT("endCursorOver")); }
void UHapbeatEventLoggerComponent::LogGrabbed()         { Emit(TEXT("grabbed")); }
void UHapbeatEventLoggerComponent::LogDropped()         { Emit(TEXT("dropped")); }
void UHapbeatEventLoggerComponent::LogActivated()       { Emit(TEXT("activated")); }
void UHapbeatEventLoggerComponent::LogDeactivated()     { Emit(TEXT("deactivated")); }
