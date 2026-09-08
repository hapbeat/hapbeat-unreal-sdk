// Copyright (c) 2026 Hapbeat. MIT License.
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HapbeatEntryRef.h"
#include "HapbeatEventMap.h"
#include "HapbeatShowcaseZ5ChargeShotActor.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHapbeatZ5EditableEventReferencesTest,
	"Hapbeat.Showcase.Z5.EditableEventReferences", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHapbeatZ5EditableEventReferencesTest::RunTest(const FString& Parameters)
{
	UClass* Class = AHapbeatShowcaseZ5ChargeShotActor::StaticClass();
	if (!TestNotNull(TEXT("Z5 showcase actor class"), Class)) { return false; }

	UWorld* World = UWorld::CreateWorld(EWorldType::Editor, false);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Editor);
	WorldContext.SetCurrentWorld(World);
	AHapbeatShowcaseZ5ChargeShotActor* Actor = World->SpawnActor<AHapbeatShowcaseZ5ChargeShotActor>(Class);
	if (!TestNotNull(TEXT("Z5 actor instance"), Actor))
	{
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
		return false;
	}

	const FObjectProperty* MapProperty = FindFProperty<FObjectProperty>(Class, TEXT("EventMapOverride"));
	UHapbeatEventMap* EventMap = MapProperty != nullptr
		? Cast<UHapbeatEventMap>(MapProperty->GetObjectPropertyValue_InContainer(Actor)) : nullptr;
	TestNotNull(TEXT("Z5 has an authored Event Map"), EventMap);

	for (const TPair<FName, FString>& Expected : {
		TPair<FName, FString>(TEXT("ChargeLoopEvent"), TEXT("showcase-kit.z5_charge_loop")),
		TPair<FName, FString>(TEXT("ChargeThresholdEvent"), TEXT("showcase-kit.z5_charge_thd")),
		TPair<FName, FString>(TEXT("ShotLightEvent"), TEXT("showcase-kit.z5_shot_light")),
		TPair<FName, FString>(TEXT("ShotHeavyEvent"), TEXT("showcase-kit.z5_shot_heavy")),
		TPair<FName, FString>(TEXT("TarHitLightEvent"), TEXT("showcase-kit.z5_tar_hit_light")),
		TPair<FName, FString>(TEXT("TarHitHeavyEvent"), TEXT("showcase-kit.z5_tar_hit_heavy")),
	})
	{
		const FStructProperty* Property = FindFProperty<FStructProperty>(Class, Expected.Key);
		const FHapbeatEntryRef* EntryRef = Property != nullptr
			? Property->ContainerPtrToValuePtr<FHapbeatEntryRef>(Actor) : nullptr;
		if (!TestNotNull(*FString::Printf(TEXT("Z5 exposes %s in Details"), *Expected.Key.ToString()), EntryRef)
			|| !TestTrue(*FString::Printf(TEXT("Z5 selects %s by default"), *Expected.Key.ToString()), EntryRef->IsSet())
			|| EventMap == nullptr)
		{
			continue;
		}

		FHapbeatEventEntry Entry;
		if (TestTrue(*FString::Printf(TEXT("Z5 resolves %s in its Event Map"), *Expected.Key.ToString()),
			EventMap->FindById(EntryRef->EntryId, Entry)))
		{
			TestEqual(*FString::Printf(TEXT("Z5 default %s"), *Expected.Key.ToString()), Entry.GetEventId(), Expected.Value);
		}
	}

	World->DestroyWorld(false);
	GEngine->DestroyWorldContext(World);
	return true;
}
#endif
