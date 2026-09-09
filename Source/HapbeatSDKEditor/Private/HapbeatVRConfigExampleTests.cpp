// Copyright (c) 2026 Hapbeat. MIT License.
#include "Misc/AutomationTest.h"

#include "Components/WidgetComponent.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"
#include "HapbeatAddressOverridePanelComponent.h"
#include "HapbeatEventMap.h"
#include "HapbeatShowcaseGameMode.h"
#include "HapbeatTriggerComponent.h"
#include "HapbeatVRConfigExampleActor.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHapbeatVRConfigExampleMapTest,
	"Hapbeat.Samples.VRConfigExample.MapWiring", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHapbeatVRConfigExampleMapTest::RunTest(const FString& Parameters)
{
	UWorld* ExampleMap = LoadObject<UWorld>(nullptr,
		TEXT("/HapbeatSDK/HapbeatSamples/VRConfigExample/Maps/VRConfigExample.VRConfigExample"));
	if (!TestNotNull(TEXT("VRConfigExample map asset"), ExampleMap))
	{
		return false;
	}

	TestTrue(TEXT("Map uses the sample's first-person GameMode"),
		ExampleMap->GetWorldSettings()->DefaultGameMode == AHapbeatShowcaseGameMode::StaticClass());

	AHapbeatVRConfigExampleActor* ConfigActor = nullptr;
	int32 ConfigActorCount = 0;
	for (TActorIterator<AHapbeatVRConfigExampleActor> It(ExampleMap); It; ++It)
	{
		ConfigActor = *It;
		++ConfigActorCount;
	}
	TestEqual(TEXT("Map has exactly one VRConfigExample actor"), ConfigActorCount, 1);
	if (!TestNotNull(TEXT("VRConfigExample actor"), ConfigActor))
	{
		return false;
	}

	UWidgetComponent* PanelSurface = ConfigActor->FindComponentByClass<UWidgetComponent>();
	UHapbeatAddressOverridePanelComponent* Panel = ConfigActor->FindComponentByClass<UHapbeatAddressOverridePanelComponent>();
	UHapbeatTriggerComponent* TestTrigger = ConfigActor->FindComponentByClass<UHapbeatTriggerComponent>();

	if (TestNotNull(TEXT("World-space panel surface"), PanelSurface))
	{
		TestEqual(TEXT("Panel uses world space"), PanelSurface->GetWidgetSpace(), EWidgetSpace::World);
		TestEqual(TEXT("Panel does not require controller ray collision"), PanelSurface->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
		ConfigActor->RerunConstructionScripts();
		TestTrue(TEXT("Panel creates its GUI in the editor viewport before PIE"), PanelSurface->GetSlateWidget().IsValid());
	}
	if (TestNotNull(TEXT("Address Override panel"), Panel))
	{
		TestTrue(TEXT("VR sample uses the compact controller layout"),
			Panel->bUseVRConfigLayout);
		TestFalse(TEXT("VR sample owns its panel visibility, so it does not expose Close"),
			Panel->bShowCloseButton);
	}
	if (TestNotNull(TEXT("100 Hz test trigger"), TestTrigger))
	{
		UHapbeatEventMap* TestMap = TestTrigger->EventMap.Get();
		TestNotNull(TEXT("100 Hz test trigger Event Map"), TestMap);
		TestTrue(TEXT("100 Hz test trigger entry is assigned"), TestTrigger->EntryId.IsValid());
		if (TestMap != nullptr)
		{
			FHapbeatEventEntry TestEntry;
			if (TestTrue(TEXT("100 Hz test trigger entry resolves"),
				TestMap->FindById(TestTrigger->EntryId, TestEntry)))
			{
				TestEqual(TEXT("100 Hz test uses Stream Clip mode"),
					TestEntry.Mode, EHapticMode::StreamClip);
				TestEqual(TEXT("100 Hz test uses the shipped one-shot entry"),
					TestEntry.DisplayName, FString(TEXT("demo_stream_sine_100hz")));
				TestFalse(TEXT("100 Hz test is one-shot"), TestEntry.bLoop);
			}
		}
	}

	return true;
}
#endif
