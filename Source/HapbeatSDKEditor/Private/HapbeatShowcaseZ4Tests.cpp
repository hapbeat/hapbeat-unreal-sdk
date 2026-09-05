// Copyright (c) 2026 Hapbeat. MIT License.
#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HapbeatParameterBinding.h"
#include "HapbeatTriggerComponent.h"
#include "HapbeatStreamPlayback.h"
#include "HapbeatShowcaseZ4ConsoleWidget.h"
#include "Widgets/Layout/SBorder.h"
#include "Styling/CoreStyle.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHapbeatZ4BindingReferencesTest,
    "Hapbeat.Showcase.Z4.BindingReferences", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHapbeatZ4BindingReferencesTest::RunTest(const FString& Parameters)
{
    UClass* Class = LoadClass<AActor>(nullptr,
        TEXT("/HapbeatSDK/HapbeatSamples/Showcase/BP_Z4_StreamConsole.BP_Z4_StreamConsole_C"));
    if (!TestNotNull(TEXT("Z4 Blueprint class"), Class)) { return false; }
    UWorld* World = UWorld::CreateWorld(EWorldType::Editor, false);
    AActor* Actor = World->SpawnActor<AActor>(Class);
    TArray<UHapbeatTriggerComponent*> Triggers;
    Actor->GetComponents(Triggers);
    UHapbeatTriggerComponent* Loop = nullptr;
    for (auto* Trigger : Triggers)
    {
        if (Trigger->GetName() == TEXT("LoopTrigger")) { Loop = Trigger; }
    }
    TestNotNull(TEXT("Runtime LoopTrigger"), Loop);
    TArray<UHapbeatParameterBinding*> Bindings;
    Actor->GetComponents(Bindings);
    TestEqual(TEXT("Two slider bindings"), Bindings.Num(), 2);
    UHapbeatParameterBinding* Gain = nullptr;
    UHapbeatParameterBinding* Pan = nullptr;
    for (auto* Binding : Bindings)
    {
        if (Binding->GetName() == TEXT("GainBinding")) { Gain = Binding; }
        if (Binding->GetName() == TEXT("PanBinding")) { Pan = Binding; }
        AddInfo(FString::Printf(TEXT("%s target=%s runtime loop=%s"), *Binding->GetName(),
            *GetPathNameSafe(Binding->TargetTrigger), *GetPathNameSafe(Loop)));
        TestTrue(*FString::Printf(TEXT("%s targets runtime LoopTrigger, not its SCS template"), *Binding->GetName()),
            Binding->TargetTrigger == Loop);
    }
    // Exercise the distributed Widget BP -> binding -> trigger -> playback ->
    // send-thread mirror path. No subsystem, network socket or audio is created.
    UClass* WidgetClass = LoadClass<UHapbeatShowcaseZ4ConsoleWidget>(nullptr,
        TEXT("/HapbeatSDK/HapbeatSamples/Showcase/BP_Z4_StreamConsoleWidget.BP_Z4_StreamConsoleWidget_C"));
    if (Loop && Gain && Pan && TestNotNull(TEXT("Widget Blueprint class"), WidgetClass))
    {
        auto* Playback = NewObject<UHapbeatStreamPlayback>(Actor);
        Playback->Init(0.5f, 1.0f);
        Playback->SetOwnerActor(Actor);
        Playback->SetActive();
        Loop->StoredPlayback = Playback;
        auto* Widget = NewObject<UHapbeatShowcaseZ4ConsoleWidget>(World, WidgetClass);
        Widget->Initialize();
        Widget->Configure(Gain, Pan, nullptr, nullptr);
        TFunction<TSharedPtr<SBorder>(TSharedRef<SWidget>)> FindPanel;
        FindPanel = [&FindPanel](TSharedRef<SWidget> Node) -> TSharedPtr<SBorder>
        {
            if (Node->GetType() == TEXT("SBorder")) { return StaticCastSharedRef<SBorder>(Node); }
            FChildren* Children = Node->GetChildren();
            for (int32 Index = 0; Index < Children->Num(); ++Index)
            {
                if (auto Found = FindPanel(Children->GetChildAt(Index))) { return Found; }
            }
            return nullptr;
        };
        auto Panel = FindPanel(Widget->TakeWidget());
        if (TestTrue(TEXT("Console has a background panel"), Panel.IsValid()))
        {
            TestTrue(TEXT("Panel uses filled WhiteBrush from pre-BP console"),
                Panel->GetBorderImage() == FCoreStyle::Get().GetBrush("WhiteBrush"));
            TestEqual(TEXT("Panel background is black at 75 percent opacity"),
                Panel->GetBorderBackgroundColor().GetSpecifiedColor(), FLinearColor(0, 0, 0, 0.75f));
        }
        Widget->HandleGainValueChanged(0.2f);
        Widget->HandlePanValueChanged(-0.75f);
        TestEqual(TEXT("Widget gain input reached binding"), Gain->GetCurrentInput(), 0.2f);
        TestEqual(TEXT("Widget pan input reached binding"), Pan->GetCurrentInput(), -0.75f);
        TestEqual(TEXT("Live playback gain is baseline x slider"), Playback->GetGain(), 0.1f);
        TestEqual(TEXT("Live playback pan follows slider"), Playback->GetPan(), -0.75f);
        TestEqual(TEXT("Send-thread gain mirror follows slider"), Playback->GetMirror()->Gain.load(), 0.1f);
        TestEqual(TEXT("Send-thread pan mirror follows slider"), Playback->GetMirror()->Pan.load(), -0.75f);
        Widget->HandleGainValueChanged(0.0f);
        Widget->HandlePanValueChanged(1.0f);
        TestEqual(TEXT("Gain zero mutes the loop"), Playback->GetGain(), 0.0f);
        TestEqual(TEXT("Pan can move to opposite endpoint"), Playback->GetPan(), 1.0f);
        Loop->StoredPlayback.Reset();
    }
    World->DestroyWorld(false);
    return true;
}
#endif
