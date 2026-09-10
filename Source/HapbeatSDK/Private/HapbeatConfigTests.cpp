// Copyright (c) 2026 Hapbeat. MIT License.
#include "HapbeatConfig.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHapbeatConfigSettingsRouteTest,
	"Hapbeat.Configuration.SettingsRoute",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHapbeatConfigSettingsRouteTest::RunTest(const FString& Parameters)
{
	const UHapbeatConfig* Config = GetDefault<UHapbeatConfig>();
	if (!TestNotNull(TEXT("Hapbeat developer settings CDO"), Config))
	{
		return false;
	}

	// Tools > Hapbeat > Hapbeat Settings and Runtime Status both use this exact
	// route. A section-name regression otherwise opens the generic Project
	// Settings page without selecting the Hapbeat settings object.
	TestEqual(TEXT("settings container"), Config->GetContainerName(), FName(TEXT("Project")));
	TestEqual(TEXT("settings category"), Config->GetCategoryName(), FName(TEXT("Plugins")));
	TestEqual(TEXT("settings section"), Config->GetSectionName(), FName(TEXT("Hapbeat")));
	return true;
}
#endif // WITH_DEV_AUTOMATION_TESTS
