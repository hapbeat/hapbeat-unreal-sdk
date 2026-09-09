// Copyright (c) 2026 Hapbeat. MIT License.
#include "Misc/AutomationTest.h"

#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"
#include "HapbeatBasicExampleActor.h"
#include "HapbeatEventMap.h"
#include "HapbeatShowcaseGameMode.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHapbeatBasicExampleMapTest,
	"Hapbeat.Samples.BasicExample.MapWiring", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHapbeatBasicExampleMapTest::RunTest(const FString& Parameters)
{
	UWorld* ExampleMap = LoadObject<UWorld>(nullptr,
		TEXT("/HapbeatSDK/HapbeatSamples/BasicExample/Maps/BasicExample.BasicExample"));
	if (!TestNotNull(TEXT("BasicExample map asset"), ExampleMap))
	{
		return false;
	}

	TestTrue(TEXT("Map uses the sample's first-person GameMode"),
		ExampleMap->GetWorldSettings()->DefaultGameMode == AHapbeatShowcaseGameMode::StaticClass());

	AHapbeatBasicExampleActor* BasicActor = nullptr;
	int32 BasicActorCount = 0;
	for (TActorIterator<AHapbeatBasicExampleActor> It(ExampleMap); It; ++It)
	{
		BasicActor = *It;
		++BasicActorCount;
	}
	TestEqual(TEXT("Map has exactly one BasicExample actor"), BasicActorCount, 1);
	if (!TestNotNull(TEXT("BasicExample actor"), BasicActor))
	{
		return false;
	}

	const FObjectProperty* MapOverrideProperty =
		FindFProperty<FObjectProperty>(AHapbeatBasicExampleActor::StaticClass(), TEXT("EventMapOverride"));
	UHapbeatEventMap* EventMap = MapOverrideProperty != nullptr
		? Cast<UHapbeatEventMap>(MapOverrideProperty->GetObjectPropertyValue_InContainer(BasicActor)) : nullptr;
	if (TestNotNull(TEXT("BasicExample actor uses the shipped Event Map"), EventMap))
	{
		TestEqual(TEXT("BasicExample Event Map has the three key bindings"), EventMap->Entries.Num(), 3);
	}

	return true;
}
#endif
