// Copyright (c) 2026 Hapbeat. MIT License.
#include "Misc/AutomationTest.h"

#include "Components/WidgetComponent.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"
#include "HapbeatAddressOverridePanelComponent.h"
#include "HapbeatShowcaseGameMode.h"
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

	if (TestNotNull(TEXT("World-space panel surface"), PanelSurface))
	{
		TestEqual(TEXT("Panel uses world space"), PanelSurface->GetWidgetSpace(), EWidgetSpace::World);
		TestEqual(TEXT("Panel does not require controller ray collision"), PanelSurface->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
		ConfigActor->RerunConstructionScripts();
		TestTrue(TEXT("Panel creates its GUI in the editor viewport before PIE"), PanelSurface->GetSlateWidget().IsValid());
	}
	if (TestNotNull(TEXT("Address Override panel"), Panel))
	{
		TestFalse(TEXT("VR sample owns its panel visibility, so it does not expose Close"),
			Panel->bShowCloseButton);
	}

	return true;
}
#endif
