// Copyright (c) 2026 Hapbeat. MIT License.

/** Editor-only authoring command for the two Blueprint-complete Showcase zones. */

#include "HapbeatShowcaseBlueprintZoneActor.h"

#include "HapbeatBlueprintLibrary.h"
#include "HapbeatEventMap.h"
#include "HapbeatParameterBinding.h"
#include "HapbeatSequenceComponent.h"
#include "HapbeatTickEmitterComponent.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Curves/CurveFloat.h"
#include "Editor.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/StaticMesh.h"
#include "Engine/TimelineTemplate.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "InputCoreTypes.h"
#include "K2Node_CallFunction.h"
#include "K2Node_InputKey.h"
#include "K2Node_Timeline.h"
#include "K2Node_VariableGet.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace HapbeatShowcaseBlueprintBuilder
{
namespace
{
constexpr TCHAR AssetFolder[] = TEXT("/HapbeatSDK/HapbeatSamples/Showcase");
constexpr TCHAR MapPath[] = TEXT("/HapbeatSDK/HapbeatSamples/Showcase/Maps/Showcase");

UHapbeatEventMap* GetShowcaseEventMap()
{
	return LoadObject<UHapbeatEventMap>(nullptr,
		TEXT("/HapbeatSDK/HapbeatSamples/Showcase/EM_Showcase.EM_Showcase"));
}

FGuid FindEntryId(const UHapbeatEventMap* Map, const TCHAR* EventName)
{
	check(Map != nullptr);
	for (const FHapbeatEventEntry& Entry : Map->Entries)
	{
		if (Entry.EventName == EventName)
		{
			return Entry.Id;
		}
	}
	UE_LOG(LogTemp, Error, TEXT("[Hapbeat] EM_Showcase has no '%s' entry."), EventName);
	return FGuid();
}

UBlueprint* LoadOrCreateBlueprint(const TCHAR* AssetName)
{
	const FString ObjectPath = FString::Printf(TEXT("%s/%s.%s"), AssetFolder, AssetName, AssetName);
	if (UBlueprint* Existing = LoadObject<UBlueprint>(nullptr, *ObjectPath))
	{
		return Existing;
	}

	const FString PackageName = FString::Printf(TEXT("%s/%s"), AssetFolder, AssetName);
	UPackage* Package = CreatePackage(*PackageName);
	return FKismetEditorUtilities::CreateBlueprint(
		AHapbeatShowcaseBlueprintZoneActor::StaticClass(), Package, FName(AssetName),
		BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass(),
		FName(TEXT("HapbeatShowcaseBlueprintBuilder")));
}

void ClearGeneratedGraph(UBlueprint* Blueprint)
{
	// Timeline templates are Blueprint-owned assets, not graph nodes.  Remove
	// them explicitly so re-running the generator remains idempotent.
	const TArray<TObjectPtr<UTimelineTemplate>> ExistingTimelines = Blueprint->Timelines;
	for (UTimelineTemplate* Timeline : ExistingTimelines)
	{
		if (Timeline != nullptr)
		{
			FBlueprintEditorUtils::RemoveTimeline(Blueprint, Timeline, true);
		}
	}

	for (UEdGraph* Graph : Blueprint->UbergraphPages)
	{
		if (Graph != nullptr)
		{
			Graph->Modify();
			Graph->Nodes.Reset();
		}
	}
}

UEdGraph* GetEventGraph(UBlueprint* Blueprint)
{
	check(Blueprint != nullptr);
	check(Blueprint->UbergraphPages.Num() > 0);
	return Blueprint->UbergraphPages[0];
}

template <typename NodeType>
NodeType* AddNode(UEdGraph* Graph, int32 X, int32 Y)
{
	NodeType* Node = NewObject<NodeType>(Graph);
	Graph->AddNode(Node, false, false);
	Node->NodePosX = X;
	Node->NodePosY = Y;
	Node->CreateNewGuid();
	Node->PostPlacedNewNode();
	Node->AllocateDefaultPins();
	return Node;
}

UEdGraphPin* FindPinChecked(UEdGraphNode* Node, const TCHAR* Name)
{
	UEdGraphPin* Pin = Node->FindPin(FName(Name));
	checkf(Pin != nullptr, TEXT("Expected pin '%s' on generated node '%s'."), Name, *Node->GetName());
	return Pin;
}

UEdGraphPin* FindTargetPinChecked(UEdGraphNode* Node)
{
	if (UEdGraphPin* Target = Node->FindPin(TEXT("Target")))
	{
		return Target;
	}
	if (UEdGraphPin* Self = Node->FindPin(UEdGraphSchema_K2::PN_Self))
	{
		return Self;
	}
	checkf(false, TEXT("Expected a Target or self pin on generated node '%s'."), *Node->GetName());
	return nullptr;
}

UK2Node_InputKey* AddKeyEvent(UEdGraph* Graph, const FKey& Key, int32 X, int32 Y)
{
	UK2Node_InputKey* Node = AddNode<UK2Node_InputKey>(Graph, X, Y);
	Node->InputKey = Key;
	Node->bConsumeInput = true;
	Node->ReconstructNode();
	return Node;
}

UK2Node_CallFunction* AddCall(UEdGraph* Graph, UClass* Class, FName Function, int32 X, int32 Y)
{
	UK2Node_CallFunction* Node = NewObject<UK2Node_CallFunction>(Graph);
	Node->FunctionReference.SetExternalMember(Function, Class);
	Graph->AddNode(Node, false, false);
	Node->NodePosX = X;
	Node->NodePosY = Y;
	Node->CreateNewGuid();
	Node->PostPlacedNewNode();
	Node->AllocateDefaultPins();
	return Node;
}

UK2Node_Timeline* AddTimeline(UEdGraph* Graph, UTimelineTemplate* Timeline, int32 X, int32 Y)
{
	check(Timeline != nullptr);
	UK2Node_Timeline* Node = NewObject<UK2Node_Timeline>(Graph);
	Node->TimelineName = Timeline->GetVariableName();
	Node->TimelineGuid = Timeline->TimelineGuid;
	Graph->AddNode(Node, false, false);
	Node->NodePosX = X;
	Node->NodePosY = Y;
	Node->CreateNewGuid();
	Node->PostPlacedNewNode();
	Node->AllocateDefaultPins();
	return Node;
}

UK2Node_VariableGet* AddComponentGet(UEdGraph* Graph, const TCHAR* ComponentName, int32 X, int32 Y)
{
	UK2Node_VariableGet* Node = AddNode<UK2Node_VariableGet>(Graph, X, Y);
	Node->VariableReference.SetSelfMember(FName(ComponentName));
	Node->ReconstructNode();
	return Node;
}

void ConnectPins(UEdGraphPin* From, UEdGraphPin* To)
{
	if (From == nullptr || To == nullptr)
	{
		UE_LOG(LogTemp, Error, TEXT("[Hapbeat] Could not find a generated Blueprint pin to link."));
		return;
	}
	if (!From->GetSchema()->TryCreateConnection(From, To))
	{
		UE_LOG(LogTemp, Error, TEXT("[Hapbeat] Could not link generated Blueprint pins '%s' -> '%s'."),
			*From->PinName.ToString(), *To->PinName.ToString());
	}
}

void ConfigurePlayEvent(UK2Node_CallFunction* Node, UHapbeatEventMap* EventMap, const FGuid& EntryId)
{
	FindPinChecked(Node, TEXT("Map"))->DefaultObject = EventMap;
	// FGuid's Blueprint text form is four uint32 fields.  Its familiar
	// {xxxxxxxx-...} display form is not accepted by ImportText for a struct pin.
	FindPinChecked(Node, TEXT("Entry"))->DefaultValue = FString::Printf(
		TEXT("(EntryId=(A=%u,B=%u,C=%u,D=%u))"), EntryId.A, EntryId.B, EntryId.C, EntryId.D);
}

UTimelineTemplate* CreateDoorMotionTimeline(UBlueprint* Blueprint)
{
	UTimelineTemplate* Timeline = FBlueprintEditorUtils::AddNewTimeline(Blueprint, TEXT("DoorMotion"));
	check(Timeline != nullptr);
	Timeline->TimelineLength = 0.65f;
	Timeline->LengthMode = TL_TimelineLength;

	FTTFloatTrack OpenAlpha;
	OpenAlpha.SetTrackName(TEXT("OpenAlpha"), Timeline);
	OpenAlpha.CurveFloat = NewObject<UCurveFloat>(Blueprint->GeneratedClass, NAME_None, RF_Public);
	const FKeyHandle ClosedKey = OpenAlpha.CurveFloat->FloatCurve.AddKey(0.0f, 0.0f);
	const FKeyHandle OpenKey = OpenAlpha.CurveFloat->FloatCurve.AddKey(0.65f, 1.0f);
	OpenAlpha.CurveFloat->FloatCurve.SetKeyInterpMode(ClosedKey, RCIM_Linear);
	OpenAlpha.CurveFloat->FloatCurve.SetKeyInterpMode(OpenKey, RCIM_Linear);
	Timeline->FloatTracks.Add(OpenAlpha);
	Timeline->AddDisplayTrack(FTTTrackId(FTTTrackBase::TT_FloatInterp, 0));
	return Timeline;
}

USCS_Node* AddSceneRoot(UBlueprint* Blueprint)
{
	USimpleConstructionScript* SCS = Blueprint->SimpleConstructionScript;
	check(SCS != nullptr);
	const TArray<USCS_Node*> ExistingRoots = SCS->GetRootNodes();
	for (USCS_Node* RootNode : ExistingRoots)
	{
		SCS->RemoveNode(RootNode);
	}
	USCS_Node* Root = SCS->CreateNode(USceneComponent::StaticClass(), TEXT("Root"));
	SCS->AddNode(Root);
	return Root;
}

USCS_Node* AddComponent(UBlueprint* Blueprint, USCS_Node* Parent, UClass* ComponentClass, const TCHAR* Name)
{
	USCS_Node* Node = Blueprint->SimpleConstructionScript->CreateNode(ComponentClass, FName(Name));
	Parent->AddChildNode(Node);
	return Node;
}

void SetMetadata(UBlueprint* Blueprint, int32 ZoneIndex, const TCHAR* Label,
	const FVector& SpawnLocation, TArray<FHapbeatShowcaseHudCommand> Commands)
{
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	AHapbeatShowcaseBlueprintZoneActor* Cdo = CastChecked<AHapbeatShowcaseBlueprintZoneActor>(
		Blueprint->GeneratedClass->GetDefaultObject());
	Cdo->ZoneIndex = ZoneIndex;
	Cdo->ZoneLabel = FText::FromString(Label);
	Cdo->PlayerSpawnRelative = FTransform(FRotator::ZeroRotator, SpawnLocation);
	Cdo->bUnlockCursorOnEnter = false;
	Cdo->HudCommands = MoveTemp(Commands);
	Blueprint->MarkPackageDirty();
}

void CreateDoorBlueprint(UBlueprint* Blueprint, UHapbeatEventMap* EventMap)
{
	// Clear stale graph nodes before the structural compile below.  In
	// particular, a prior generator version may have referenced an SCS variable
	// that is about to be rebuilt.
	ClearGeneratedGraph(Blueprint);
	USCS_Node* Root = AddSceneRoot(Blueprint);
	USCS_Node* HingeNode = AddComponent(Blueprint, Root, USceneComponent::StaticClass(), TEXT("DoorHinge"));
	USceneComponent* Hinge = CastChecked<USceneComponent>(HingeNode->ComponentTemplate);
	Hinge->SetRelativeLocation(FVector(0.0f, -66.1f, 0.0f));

	USCS_Node* FrameNode = AddComponent(Blueprint, Root, UStaticMeshComponent::StaticClass(), TEXT("DoorFrameMesh"));
	UStaticMeshComponent* Frame = CastChecked<UStaticMeshComponent>(FrameNode->ComponentTemplate);
	Frame->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,
		TEXT("/HapbeatSDK/HapbeatSamples/Showcase/Meshes/SM_DoorFrame.SM_DoorFrame")));
	Frame->SetRelativeRotation(FRotator(0.0f, -90.0f, 0.0f));
	Frame->SetCollisionProfileName(TEXT("BlockAll"));

	USCS_Node* LeafNode = AddComponent(Blueprint, HingeNode, UStaticMeshComponent::StaticClass(), TEXT("DoorLeafMesh"));
	UStaticMeshComponent* Leaf = CastChecked<UStaticMeshComponent>(LeafNode->ComponentTemplate);
	Leaf->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,
		TEXT("/HapbeatSDK/HapbeatSamples/Showcase/Meshes/SM_Door.SM_Door")));
	Leaf->SetRelativeLocation(FVector(-66.1f, 0.0f, 0.0f));
	Leaf->SetCollisionProfileName(TEXT("BlockAll"));
	Leaf->SetMobility(EComponentMobility::Movable);

	USCS_Node* HandleNode = AddComponent(Blueprint, LeafNode, UStaticMeshComponent::StaticClass(), TEXT("DoorHandleMesh"));
	UStaticMeshComponent* Handle = CastChecked<UStaticMeshComponent>(HandleNode->ComponentTemplate);
	Handle->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,
		TEXT("/HapbeatSDK/HapbeatSamples/Showcase/Meshes/SM_DoorHandle.SM_DoorHandle")));
	Handle->SetCollisionProfileName(TEXT("NoCollision"));

	// Compile once after creating SCS variables so the graph's component-get
	// nodes resolve their generated member references before we wire them.
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	UEdGraph* Graph = GetEventGraph(Blueprint);
	const FGuid OpenId = FindEntryId(EventMap, TEXT("z2_door_open"));
	const FGuid CloseId = FindEntryId(EventMap, TEXT("z2_door_close"));
	const FGuid LockId = FindEntryId(EventMap, TEXT("z2_door_lock"));
	UTimelineTemplate* DoorMotionTemplate = CreateDoorMotionTimeline(Blueprint);
	UK2Node_Timeline* DoorMotion = AddTimeline(Graph, DoorMotionTemplate, 0, -120);
	UK2Node_CallFunction* LerpRotation = AddCall(Graph, UKismetMathLibrary::StaticClass(),
		GET_FUNCTION_NAME_CHECKED(UKismetMathLibrary, RLerp), 280, -120);
	FindPinChecked(LerpRotation, TEXT("A"))->DefaultValue = TEXT("(Pitch=0.000000,Yaw=0.000000,Roll=0.000000)");
	FindPinChecked(LerpRotation, TEXT("B"))->DefaultValue = TEXT("(Pitch=0.000000,Yaw=90.000000,Roll=0.000000)");
	FindPinChecked(LerpRotation, TEXT("bShortestPath"))->DefaultValue = TEXT("false");
	UK2Node_VariableGet* HingeGet = AddComponentGet(Graph, TEXT("DoorHinge"), 280, 40);
	UK2Node_CallFunction* SetHingeRotation = AddCall(Graph, USceneComponent::StaticClass(),
		GET_FUNCTION_NAME_CHECKED(USceneComponent, K2_SetRelativeRotation), 560, -30);
	ConnectPins(DoorMotion->GetUpdatePin(), SetHingeRotation->GetExecPin());
	ConnectPins(FindPinChecked(DoorMotion, TEXT("OpenAlpha")), FindPinChecked(LerpRotation, TEXT("Alpha")));
	ConnectPins(LerpRotation->GetReturnValuePin(), FindPinChecked(SetHingeRotation, TEXT("NewRotation")));
	ConnectPins(FindPinChecked(HingeGet, TEXT("DoorHinge")), FindTargetPinChecked(SetHingeRotation));

	auto AddDoorAction = [&](const FKey& Key, const FGuid& EventId, UEdGraphPin* MotionPin, int32 Y)
	{
		UK2Node_InputKey* Input = AddKeyEvent(Graph, Key, -800, Y);
		UK2Node_CallFunction* Play = AddCall(Graph, UHapbeatBlueprintLibrary::StaticClass(),
			GET_FUNCTION_NAME_CHECKED(UHapbeatBlueprintLibrary, PlayHapbeatEvent), -500, Y);
		ConfigurePlayEvent(Play, EventMap, EventId);
		ConnectPins(FindPinChecked(Input, TEXT("Pressed")), Play->GetExecPin());
		ConnectPins(Play->GetThenPin(), MotionPin);
	};

	AddDoorAction(EKeys::F, OpenId, DoorMotion->GetPlayFromStartPin(), -180);
	AddDoorAction(EKeys::G, CloseId, DoorMotion->GetReversePin(), 20);
	UK2Node_InputKey* LockInput = AddKeyEvent(Graph, EKeys::L, -800, 220);
	UK2Node_CallFunction* LockPlay = AddCall(Graph, UHapbeatBlueprintLibrary::StaticClass(),
		GET_FUNCTION_NAME_CHECKED(UHapbeatBlueprintLibrary, PlayHapbeatEvent), -500, 220);
	ConfigurePlayEvent(LockPlay, EventMap, LockId);
	ConnectPins(FindPinChecked(LockInput, TEXT("Pressed")), LockPlay->GetExecPin());

	SetMetadata(Blueprint, 2, TEXT("Door"), FVector(-400.0f, 20.0f, 0.0f),
		{ { FText::FromString(TEXT("F")), FText::FromString(TEXT("open door + Play Hapbeat Event")) },
		  { FText::FromString(TEXT("G")), FText::FromString(TEXT("close door + Play Hapbeat Event")) },
		  { FText::FromString(TEXT("L")), FText::FromString(TEXT("lock haptic event")) } });
}

void CreateStreamConsoleBlueprint(UBlueprint* Blueprint, UHapbeatEventMap* EventMap)
{
	ClearGeneratedGraph(Blueprint);
	USCS_Node* Root = AddSceneRoot(Blueprint);
	const FGuid LoopId = FindEntryId(EventMap, TEXT("z4_stream_loop"));
	const FGuid TickId = FindEntryId(EventMap, TEXT("z4_slider_tick"));

	USCS_Node* SequenceNode = AddComponent(Blueprint, Root, UHapbeatSequenceComponent::StaticClass(), TEXT("StreamLoop"));
	UHapbeatSequenceComponent* Sequence = CastChecked<UHapbeatSequenceComponent>(SequenceNode->ComponentTemplate);
	Sequence->EventMap = EventMap;
	Sequence->EntryId = LoopId;

	USCS_Node* TickNode = AddComponent(Blueprint, Root, UHapbeatTickEmitterComponent::StaticClass(), TEXT("SliderTick"));
	UHapbeatTickEmitterComponent* Tick = CastChecked<UHapbeatTickEmitterComponent>(TickNode->ComponentTemplate);
	Tick->EventMap = EventMap;
	Tick->EntryId = TickId;
	Tick->TickThreshold = 0.1f;

	auto AddBinding = [&](const TCHAR* Name, EHapbeatBindingOutput Output, float InMin, float InMax, float OutMin, float OutMax)
	{
		USCS_Node* Node = AddComponent(Blueprint, Root, UHapbeatParameterBinding::StaticClass(), Name);
		UHapbeatParameterBinding* Binding = CastChecked<UHapbeatParameterBinding>(Node->ComponentTemplate);
		Binding->SourceProperty = EHapbeatBindingSource::External;
		Binding->OutputParameter = Output;
		Binding->InputMin = InMin;
		Binding->InputMax = InMax;
		Binding->OutputMin = OutMin;
		Binding->OutputMax = OutMax;
		Binding->TargetTrigger = Sequence;
	};
	AddBinding(TEXT("GainBinding"), EHapbeatBindingOutput::StreamGain, 0.0f, 1.0f, 0.0f, 1.0f);
	AddBinding(TEXT("PanBinding"), EHapbeatBindingOutput::StreamPan, -1.0f, 1.0f, -1.0f, 1.0f);

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	UEdGraph* Graph = GetEventGraph(Blueprint);
	auto AddEventCall = [&](const FKey& Key, const FGuid& EventId, int32 Y)
	{
		UK2Node_InputKey* Input = AddKeyEvent(Graph, Key, -800, Y);
		UK2Node_CallFunction* Call = AddCall(Graph, UHapbeatBlueprintLibrary::StaticClass(),
			GET_FUNCTION_NAME_CHECKED(UHapbeatBlueprintLibrary, PlayHapbeatEvent), -500, Y);
		ConfigurePlayEvent(Call, EventMap, EventId);
		ConnectPins(FindPinChecked(Input, TEXT("Pressed")), Call->GetExecPin());
	};

	AddEventCall(EKeys::F, LoopId, -180);
	UK2Node_InputKey* StopInput = AddKeyEvent(Graph, EKeys::G, -800, 0);
	UK2Node_CallFunction* Stop = AddCall(Graph, UHapbeatBlueprintLibrary::StaticClass(),
		GET_FUNCTION_NAME_CHECKED(UHapbeatBlueprintLibrary, StopHapbeatEvent), -500, 0);
	ConfigurePlayEvent(Stop, EventMap, LoopId);
	ConnectPins(FindPinChecked(StopInput, TEXT("Pressed")), Stop->GetExecPin());
	AddEventCall(EKeys::T, TickId, 180);

	SetMetadata(Blueprint, 4, TEXT("Stream Console"), FVector(-250.0f, 0.0f, 0.0f),
		{ { FText::FromString(TEXT("F / G")), FText::FromString(TEXT("start / stop StreamClip")) },
		  { FText::FromString(TEXT("T")), FText::FromString(TEXT("fire tick event")) },
		  { FText::FromString(TEXT("Components")), FText::FromString(TEXT("Sequence, Gain/Pan bindings, Tick Trigger are editable on this Blueprint")) } });
}

void ReplaceZoneActor(UWorld* World, const TCHAR* Label, UBlueprint* Blueprint, const FVector& DefaultLocation)
{
	FTransform Transform(FRotator::ZeroRotator, DefaultLocation);
	TArray<TWeakObjectPtr<AActor>> OldActors;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (Actor != nullptr && Actor->GetActorLabel() == Label)
		{
			Transform = Actor->GetActorTransform();
			OldActors.Add(Actor);
		}
	}
	FActorSpawnParameters Params;
	AActor* Replacement = World->SpawnActor<AActor>(Blueprint->GeneratedClass, Transform, Params);
	checkf(Replacement != nullptr, TEXT("Could not spawn %s"), Label);
	Replacement->SetActorLabel(Label);
	for (const TWeakObjectPtr<AActor> OldActor : OldActors)
	{
		if (OldActor.IsValid())
		{
			World->EditorDestroyActor(OldActor.Get(), true);
		}
	}
}
}

void Generate()
{
	UHapbeatEventMap* EventMap = GetShowcaseEventMap();
	checkf(EventMap != nullptr, TEXT("Could not load EM_Showcase."));
	UBlueprint* Door = LoadOrCreateBlueprint(TEXT("BP_Z2_Door"));
	UBlueprint* Stream = LoadOrCreateBlueprint(TEXT("BP_Z4_StreamConsole"));
	check(Door != nullptr && Stream != nullptr);
	CreateDoorBlueprint(Door, EventMap);
	CreateStreamConsoleBlueprint(Stream, EventMap);
	FKismetEditorUtilities::CompileBlueprint(Door);
	FKismetEditorUtilities::CompileBlueprint(Stream);
	FAssetRegistryModule::AssetCreated(Door);
	FAssetRegistryModule::AssetCreated(Stream);
	Door->MarkPackageDirty();
	Stream->MarkPackageDirty();
	UPackage::SavePackage(Door->GetOutermost(), Door, *FPackageName::LongPackageNameToFilename(Door->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension()), FSavePackageArgs());
	UPackage::SavePackage(Stream->GetOutermost(), Stream, *FPackageName::LongPackageNameToFilename(Stream->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension()), FSavePackageArgs());

	UWorld* World = GEditor->GetEditorWorldContext().World();
	checkf(World != nullptr && World->GetOutermost()->GetName() == MapPath, TEXT("Open the Showcase map before running Hapbeat.GenerateBlueprintShowcase."));
	ReplaceZoneActor(World, TEXT("Z2_Door"), Door, FVector(0.0f, 3000.0f, 0.0f));
	ReplaceZoneActor(World, TEXT("Z4_StreamConsole"), Stream, FVector(0.0f, 9000.0f, 0.0f));
	FEditorFileUtils::SaveLevel(World->PersistentLevel);
	UE_LOG(LogTemp, Display, TEXT("[Hapbeat] Generated BP_Z2_Door and BP_Z4_StreamConsole; replaced the two Showcase actors."));
}

static FAutoConsoleCommand GenerateCommand(
	TEXT("Hapbeat.GenerateBlueprintShowcase"),
	TEXT("Generate the two Blueprint-authored Showcase zones and replace their map actors."),
	FConsoleCommandDelegate::CreateStatic(&Generate));
}
