// Copyright (c) 2026 Hapbeat. MIT License.
#include "Misc/AutomationTest.h"

#include "Components/WidgetComponent.h"
#include "Components/WidgetInteractionComponent.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"
#include "HapbeatAddressOverridePanelComponent.h"
#include "HapbeatShowcaseGameMode.h"
#include "HapbeatVRConfigExampleActor.h"
#include "MotionControllerComponent.h"

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
	UMotionControllerComponent* RightController = ConfigActor->FindComponentByClass<UMotionControllerComponent>();
	UWidgetInteractionComponent* Interaction = ConfigActor->FindComponentByClass<UWidgetInteractionComponent>();
	UHapbeatAddressOverridePanelComponent* Panel = ConfigActor->FindComponentByClass<UHapbeatAddressOverridePanelComponent>();

	if (TestNotNull(TEXT("World-space panel surface"), PanelSurface))
	{
		TestEqual(TEXT("Panel uses world space"), PanelSurface->GetWidgetSpace(), EWidgetSpace::World);
		TestEqual(TEXT("Panel accepts only query traces"), PanelSurface->GetCollisionEnabled(), ECollisionEnabled::QueryOnly);
		TestEqual(TEXT("Panel accepts the UI Visibility trace"),
			PanelSurface->GetCollisionResponseToChannel(ECC_Visibility), ECR_Block);
	}
	if (TestNotNull(TEXT("Right-hand motion controller"), RightController))
	{
		TestEqual(TEXT("Controller uses OpenXR standard Right source"),
			RightController->GetTrackingMotionSource(), FName(TEXT("Right")));
	}
	if (TestNotNull(TEXT("Widget interaction ray"), Interaction))
	{
		TestEqual(TEXT("Interaction traces from the controller in world space"),
			Interaction->InteractionSource, EWidgetInteractionSource::World);
		TestEqual(TEXT("Interaction traces the panel's Visibility channel"),
			Interaction->TraceChannel, TEnumAsByte<ECollisionChannel>(ECC_Visibility));
	}
	if (TestNotNull(TEXT("Address Override panel"), Panel))
	{
		TestFalse(TEXT("VR sample owns its panel visibility, so it does not expose Close"),
			Panel->bShowCloseButton);
	}

	return true;
}
#endif
