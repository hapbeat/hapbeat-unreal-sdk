// Copyright (c) 2026 Hapbeat. MIT License.
#include "Misc/AutomationTest.h"

#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"
#include "HapbeatShowcaseActor.h"
#include "HapbeatShowcaseGameMode.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHapbeatShowcaseMapTest,
	"Hapbeat.Samples.Showcase.MapWiring", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHapbeatShowcaseMapTest::RunTest(const FString& Parameters)
{
	UWorld* ShowcaseMap = LoadObject<UWorld>(nullptr,
		TEXT("/HapbeatSDK/HapbeatSamples/Showcase/Maps/Showcase.Showcase"));
	if (!TestNotNull(TEXT("Showcase map asset"), ShowcaseMap))
	{
		return false;
	}

	TestTrue(TEXT("Map uses the sample's first-person GameMode"),
		ShowcaseMap->GetWorldSettings()->DefaultGameMode == AHapbeatShowcaseGameMode::StaticClass());

	int32 ShowcaseActorCount = 0;
	for (TActorIterator<AHapbeatShowcaseActor> It(ShowcaseMap); It; ++It)
	{
		++ShowcaseActorCount;
	}
	TestEqual(TEXT("Map has exactly one Showcase switcher"), ShowcaseActorCount, 1);

	return true;
}
#endif
