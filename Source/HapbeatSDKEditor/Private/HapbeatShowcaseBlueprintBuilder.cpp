// Copyright (c) 2026 Hapbeat. MIT License.

/** Editor-only authoring command for the two Blueprint-complete Showcase zones. */

#include "HapbeatShowcaseBlueprintBuilder.h"

#include "HapbeatShowcaseBlueprintZoneActor.h"
#include "HapbeatShowcaseZ4StreamConsoleActor.h"

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
#include "K2Node_IfThenElse.h"
#include "K2Node_InputKey.h"
#include "K2Node_Timeline.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Materials/MaterialInterface.h"
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
	// These are state variables owned by the generated Z2 graph.  Remove them
	// before rebuilding so the command is idempotent and their defaults cannot
	// drift from the graph it creates.
	for (const FName VariableName : { FName(TEXT("bDoorOpen")), FName(TEXT("bDoorLocked")), FName(TEXT("bDoorMoving")) })
	{
		FBlueprintEditorUtils::RemoveMemberVariable(Blueprint, VariableName);
	}

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

UK2Node_VariableGet* AddSelfVariableGet(UEdGraph* Graph, const TCHAR* VariableName, int32 X, int32 Y)
{
	UK2Node_VariableGet* Node = AddNode<UK2Node_VariableGet>(Graph, X, Y);
	Node->VariableReference.SetSelfMember(FName(VariableName));
	Node->ReconstructNode();
	return Node;
}

UK2Node_VariableSet* AddBoolSet(UEdGraph* Graph, const TCHAR* VariableName, bool bValue, int32 X, int32 Y)
{
	UK2Node_VariableSet* Node = AddNode<UK2Node_VariableSet>(Graph, X, Y);
	Node->VariableReference.SetSelfMember(FName(VariableName));
	Node->ReconstructNode();
	// UK2Node_Variable::GetValuePin() is intentionally getter-only.  A setter
	// owns an input pin with the member's name instead.
	UEdGraphPin* ValuePin = Node->FindPin(FName(VariableName), EGPD_Input);
	checkf(ValuePin != nullptr, TEXT("Expected value input for generated bool '%s'."), VariableName);
	ValuePin->DefaultValue = bValue ? TEXT("true") : TEXT("false");
	return Node;
}

UK2Node_IfThenElse* AddBranch(UEdGraph* Graph, int32 X, int32 Y)
{
	return AddNode<UK2Node_IfThenElse>(Graph, X, Y);
}

void AddBoolMember(UBlueprint* Blueprint, const TCHAR* VariableName)
{
	FEdGraphPinType BoolType;
	BoolType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
	checkf(FBlueprintEditorUtils::AddMemberVariable(Blueprint, FName(VariableName), BoolType, TEXT("false")),
		TEXT("Could not add generated boolean '%s'."), VariableName);
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
	// The graph pin parser reads FGuid's four fields through signed integer
	// properties.  Writing a high-bit component as uint32 clamps it to
	// INT32_MAX, silently changing only some event IDs (for example the Z2 close
	// entry).  Its familiar {xxxxxxxx-...} display form is not accepted either.
	FindPinChecked(Node, TEXT("Entry"))->DefaultValue = FString::Printf(
		TEXT("(EntryId=(A=%d,B=%d,C=%d,D=%d))"),
		static_cast<int32>(EntryId.A), static_cast<int32>(EntryId.B),
		static_cast<int32>(EntryId.C), static_cast<int32>(EntryId.D));
}

UTimelineTemplate* CreateDoorMotionTimeline(UBlueprint* Blueprint, const TCHAR* Name,
	float DurationSeconds, float StartAlpha, float EndAlpha)
{
	UTimelineTemplate* Timeline = FBlueprintEditorUtils::AddNewTimeline(Blueprint, Name);
	check(Timeline != nullptr);
	Timeline->TimelineLength = DurationSeconds;
	Timeline->LengthMode = TL_TimelineLength;

	FTTFloatTrack OpenAlpha;
	OpenAlpha.SetTrackName(TEXT("OpenAlpha"), Timeline);
	OpenAlpha.CurveFloat = NewObject<UCurveFloat>(Blueprint->GeneratedClass, NAME_None, RF_Public);
	const FKeyHandle ClosedKey = OpenAlpha.CurveFloat->FloatCurve.AddKey(0.0f, StartAlpha);
	const FKeyHandle OpenKey = OpenAlpha.CurveFloat->FloatCurve.AddKey(DurationSeconds, EndAlpha);
	OpenAlpha.CurveFloat->FloatCurve.SetKeyInterpMode(ClosedKey, RCIM_Linear);
	OpenAlpha.CurveFloat->FloatCurve.SetKeyInterpMode(OpenKey, RCIM_Linear);
	Timeline->FloatTracks.Add(OpenAlpha);
	Timeline->AddDisplayTrack(FTTTrackId(FTTTrackBase::TT_FloatInterp, 0));
	return Timeline;
}

UTimelineTemplate* CreateDoorRattleTimeline(UBlueprint* Blueprint)
{
	UTimelineTemplate* Timeline = FBlueprintEditorUtils::AddNewTimeline(Blueprint, TEXT("DoorRattle"));
	check(Timeline != nullptr);
	Timeline->TimelineLength = 0.3f;
	Timeline->LengthMode = TL_TimelineLength;

	FTTFloatTrack RattleYaw;
	RattleYaw.SetTrackName(TEXT("RattleYaw"), Timeline);
	RattleYaw.CurveFloat = NewObject<UCurveFloat>(Blueprint->GeneratedClass, NAME_None, RF_Public);
	const FKeyHandle Key0 = RattleYaw.CurveFloat->FloatCurve.AddKey(0.0f, 0.0f);
	const FKeyHandle Key1 = RattleYaw.CurveFloat->FloatCurve.AddKey(0.1f, 3.0f);
	const FKeyHandle Key2 = RattleYaw.CurveFloat->FloatCurve.AddKey(0.2f, -3.0f);
	const FKeyHandle Key3 = RattleYaw.CurveFloat->FloatCurve.AddKey(0.3f, 0.0f);
	for (const FKeyHandle Key : { Key0, Key1, Key2, Key3 })
	{
		RattleYaw.CurveFloat->FloatCurve.SetKeyInterpMode(Key, RCIM_Linear);
	}
	Timeline->FloatTracks.Add(RattleYaw);
	Timeline->AddDisplayTrack(FTTTrackId(FTTTrackBase::TT_FloatInterp, 0));
	return Timeline;
}

USCS_Node* AddSceneRoot(UBlueprint* Blueprint)
{
	USimpleConstructionScript* SCS = Blueprint->SimpleConstructionScript;
	check(SCS != nullptr);

	// Removing just a root node does not remove its descendants from the SCS's
	// flat node store.  Re-running the generator then instantiates the detached
	// templates as well as the fresh three nodes, which is how a single door
	// accumulated overlapping leaves. Remove every existing node, children first,
	// before creating the replacement root.
	const TArray<USCS_Node*> ExistingNodes = SCS->GetAllNodes();
	for (int32 Index = ExistingNodes.Num() - 1; Index >= 0; --Index)
	{
		if (USCS_Node* Node = ExistingNodes[Index]; Node != nullptr && SCS->GetAllNodes().Contains(Node))
		{
			SCS->RemoveNode(Node, /*bValidateSceneRootNodes=*/false);
		}
	}
	SCS->ValidateSceneRootNodes();
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

void ApplyMaterialToAllSlots(UStaticMeshComponent* Component, UMaterialInterface* Material)
{
	check(Component != nullptr);
	if (Material == nullptr)
	{
		return;
	}

	// A mesh's material-slot count belongs to the mesh, not to its component
	// override array.  Applying only slot 0 left the decorative frame's other
	// authored slots on their import material, which made it visibly differ
	// from the leaf despite using the same Showcase material by design.
	for (int32 SlotIndex = 0; SlotIndex < Component->GetNumMaterials(); ++SlotIndex)
	{
		Component->SetMaterial(SlotIndex, Material);
	}
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
	// The timelines rotate this pivot at runtime. Scene components default to
	// Static, which silently rejects those updates even when the leaf itself is
	// Movable.
	Hinge->SetMobility(EComponentMobility::Movable);
	Hinge->SetRelativeLocation(FVector(0.0f, -66.1f, 0.0f));
	Hinge->SetRelativeRotation(FRotator(0.0f, -90.0f, 0.0f));
	UMaterialInterface* DoorMaterial = LoadObject<UMaterialInterface>(nullptr,
		TEXT("/HapbeatSDK/HapbeatSamples/Showcase/Materials/MI_DefaultMaterial.MI_DefaultMaterial"));

	USCS_Node* FrameNode = AddComponent(Blueprint, Root, UStaticMeshComponent::StaticClass(), TEXT("DoorFrameMesh"));
	UStaticMeshComponent* Frame = CastChecked<UStaticMeshComponent>(FrameNode->ComponentTemplate);
	Frame->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,
		TEXT("/HapbeatSDK/HapbeatSamples/Showcase/Meshes/SM_DoorFrame.SM_DoorFrame")));
	Frame->SetRelativeRotation(FRotator(0.0f, -90.0f, 0.0f));
	Frame->SetCollisionProfileName(TEXT("BlockAll"));
	Frame->SetMobility(EComponentMobility::Movable);
	ApplyMaterialToAllSlots(Frame, DoorMaterial);

	USCS_Node* LeafNode = AddComponent(Blueprint, HingeNode, UStaticMeshComponent::StaticClass(), TEXT("DoorLeafMesh"));
	UStaticMeshComponent* Leaf = CastChecked<UStaticMeshComponent>(LeafNode->ComponentTemplate);
	Leaf->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,
		TEXT("/HapbeatSDK/HapbeatSamples/Showcase/Meshes/SM_Door.SM_Door")));
	Leaf->SetRelativeLocation(FVector(-66.1f, 0.0f, 0.0f));
	Leaf->SetCollisionProfileName(TEXT("BlockAll"));
	Leaf->SetMobility(EComponentMobility::Movable);
	ApplyMaterialToAllSlots(Leaf, DoorMaterial);

	USCS_Node* HandleNode = AddComponent(Blueprint, LeafNode, UStaticMeshComponent::StaticClass(), TEXT("DoorHandleMesh"));
	UStaticMeshComponent* Handle = CastChecked<UStaticMeshComponent>(HandleNode->ComponentTemplate);
	Handle->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,
		TEXT("/HapbeatSDK/HapbeatSamples/Showcase/Meshes/SM_DoorHandle.SM_DoorHandle")));
	Handle->SetCollisionProfileName(TEXT("NoCollision"));
	Handle->SetMobility(EComponentMobility::Movable);
	ApplyMaterialToAllSlots(Handle, DoorMaterial);

	// Compile once after creating SCS variables so the graph's component-get
	// nodes resolve their generated member references before we wire them.
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	UEdGraph* Graph = GetEventGraph(Blueprint);
	const FGuid OpenId = FindEntryId(EventMap, TEXT("z2_door_open"));
	const FGuid CloseId = FindEntryId(EventMap, TEXT("z2_door_close"));
	const FGuid SlamId = FindEntryId(EventMap, TEXT("z2_door_slam"));
	const FGuid LockId = FindEntryId(EventMap, TEXT("z2_door_lock"));
	const FGuid UnlockId = FindEntryId(EventMap, TEXT("z2_door_unlock"));
	const FGuid RattleId = FindEntryId(EventMap, TEXT("z2_door_rattle"));
	AddBoolMember(Blueprint, TEXT("bDoorOpen"));
	AddBoolMember(Blueprint, TEXT("bDoorLocked"));
	AddBoolMember(Blueprint, TEXT("bDoorMoving"));

	// Z2 is deliberately a Blueprint-complete example.  The three motion
	// timelines preserve the prior C++ timings: open (2.0 s), normal close
	// (2.2 s), and slam (0.117 s).  A separate rattle timeline keeps the locked
	// feedback readable in the graph instead of hiding it in native code.
	UTimelineTemplate* OpenTemplate = CreateDoorMotionTimeline(Blueprint, TEXT("DoorOpen"), 2.0f, 0.0f, 1.0f);
	UTimelineTemplate* CloseTemplate = CreateDoorMotionTimeline(Blueprint, TEXT("DoorClose"), 2.2f, 1.0f, 0.0f);
	UTimelineTemplate* SlamTemplate = CreateDoorMotionTimeline(Blueprint, TEXT("DoorSlam"), 0.117f, 1.0f, 0.0f);
	UTimelineTemplate* RattleTemplate = CreateDoorRattleTimeline(Blueprint);
	UK2Node_Timeline* DoorOpen = AddTimeline(Graph, OpenTemplate, 200, -420);
	UK2Node_Timeline* DoorClose = AddTimeline(Graph, CloseTemplate, 200, -180);
	UK2Node_Timeline* DoorSlam = AddTimeline(Graph, SlamTemplate, 200, 60);
	UK2Node_Timeline* DoorRattle = AddTimeline(Graph, RattleTemplate, 200, 300);
	UK2Node_VariableGet* HingeGet = AddComponentGet(Graph, TEXT("DoorHinge"), 620, -490);

	auto AddMotionRotation = [&](UK2Node_Timeline* Timeline, int32 Y)
	{
		UK2Node_CallFunction* LerpRotation = AddCall(Graph, UKismetMathLibrary::StaticClass(),
			GET_FUNCTION_NAME_CHECKED(UKismetMathLibrary, RLerp), 620, Y);
		FindPinChecked(LerpRotation, TEXT("A"))->DefaultValue = TEXT("0.000000,-90.000000,0.000000");
		FindPinChecked(LerpRotation, TEXT("B"))->DefaultValue = TEXT("0.000000,0.000000,0.000000");
		FindPinChecked(LerpRotation, TEXT("bShortestPath"))->DefaultValue = TEXT("false");
		UK2Node_CallFunction* SetRotation = AddCall(Graph, USceneComponent::StaticClass(),
			GET_FUNCTION_NAME_CHECKED(USceneComponent, K2_SetRelativeRotation), 900, Y);
		ConnectPins(Timeline->GetUpdatePin(), SetRotation->GetExecPin());
		ConnectPins(FindPinChecked(Timeline, TEXT("OpenAlpha")), FindPinChecked(LerpRotation, TEXT("Alpha")));
		ConnectPins(LerpRotation->GetReturnValuePin(), FindPinChecked(SetRotation, TEXT("NewRotation")));
		ConnectPins(FindPinChecked(HingeGet, TEXT("DoorHinge")), FindTargetPinChecked(SetRotation));
	};
	AddMotionRotation(DoorOpen, -420);
	AddMotionRotation(DoorClose, -180);
	AddMotionRotation(DoorSlam, 60);

	UK2Node_CallFunction* MakeRattleRotation = AddCall(Graph, UKismetMathLibrary::StaticClass(),
		GET_FUNCTION_NAME_CHECKED(UKismetMathLibrary, MakeRotator), 620, 300);
	UK2Node_CallFunction* AddRattleYaw = AddCall(Graph, UKismetMathLibrary::StaticClass(),
		GET_FUNCTION_NAME_CHECKED(UKismetMathLibrary, Add_FloatFloat), 460, 300);
	UK2Node_CallFunction* SetRattleRotation = AddCall(Graph, USceneComponent::StaticClass(),
		GET_FUNCTION_NAME_CHECKED(USceneComponent, K2_SetRelativeRotation), 900, 300);
	FindPinChecked(AddRattleYaw, TEXT("A"))->DefaultValue = TEXT("-90.000000");
	FindPinChecked(MakeRattleRotation, TEXT("Pitch"))->DefaultValue = TEXT("0.000000");
	FindPinChecked(MakeRattleRotation, TEXT("Roll"))->DefaultValue = TEXT("0.000000");
	ConnectPins(DoorRattle->GetUpdatePin(), SetRattleRotation->GetExecPin());
	ConnectPins(FindPinChecked(DoorRattle, TEXT("RattleYaw")), FindPinChecked(AddRattleYaw, TEXT("B")));
	ConnectPins(AddRattleYaw->GetReturnValuePin(), FindPinChecked(MakeRattleRotation, TEXT("Yaw")));
	ConnectPins(MakeRattleRotation->GetReturnValuePin(), FindPinChecked(SetRattleRotation, TEXT("NewRotation")));
	ConnectPins(FindPinChecked(HingeGet, TEXT("DoorHinge")), FindTargetPinChecked(SetRattleRotation));

	// Moving input is ignored, matching the previous C++ state machine.  Each
	// terminal timeline clears the guard when it reaches its closed/open state.
	UK2Node_VariableSet* StopOpening = AddBoolSet(Graph, TEXT("bDoorMoving"), false, 1180, -420);
	UK2Node_VariableSet* StopClosing = AddBoolSet(Graph, TEXT("bDoorMoving"), false, 1180, -180);
	UK2Node_VariableSet* StopSlamming = AddBoolSet(Graph, TEXT("bDoorMoving"), false, 1180, 60);
	ConnectPins(DoorOpen->GetFinishedPin(), StopOpening->GetExecPin());
	ConnectPins(DoorClose->GetFinishedPin(), StopClosing->GetExecPin());
	ConnectPins(DoorSlam->GetFinishedPin(), StopSlamming->GetExecPin());

	auto AddPlay = [&](const FGuid& EventId, int32 X, int32 Y)
	{
		UK2Node_CallFunction* Play = AddCall(Graph, UHapbeatBlueprintLibrary::StaticClass(),
			GET_FUNCTION_NAME_CHECKED(UHapbeatBlueprintLibrary, PlayHapbeatEvent), X, Y);
		ConfigurePlayEvent(Play, EventMap, EventId);
		return Play;
	};
	auto AddRattle = [&](int32 X, int32 Y)
	{
		UK2Node_CallFunction* RattlePlay = AddPlay(RattleId, X, Y);
		ConnectPins(RattlePlay->GetThenPin(), DoorRattle->GetPlayFromStartPin());
		return RattlePlay;
	};

	// F: open when closed, close when open, and rattle while locked.
	UK2Node_InputKey* ToggleInput = AddKeyEvent(Graph, EKeys::F, -1250, -220);
	UK2Node_IfThenElse* ToggleMoving = AddBranch(Graph, -1040, -220);
	UK2Node_VariableGet* ToggleMovingGet = AddSelfVariableGet(Graph, TEXT("bDoorMoving"), -1040, -130);
	UK2Node_IfThenElse* ToggleLocked = AddBranch(Graph, -800, -220);
	UK2Node_VariableGet* ToggleLockedGet = AddSelfVariableGet(Graph, TEXT("bDoorLocked"), -800, -130);
	UK2Node_IfThenElse* ToggleOpen = AddBranch(Graph, -540, -220);
	UK2Node_VariableGet* ToggleOpenGet = AddSelfVariableGet(Graph, TEXT("bDoorOpen"), -540, -130);
	UK2Node_CallFunction* ToggleRattle = AddRattle(-300, -330);
	UK2Node_CallFunction* ClosePlay = AddPlay(CloseId, -300, -220);
	UK2Node_VariableSet* SetClosed = AddBoolSet(Graph, TEXT("bDoorOpen"), false, -80, -220);
	UK2Node_VariableSet* StartClosing = AddBoolSet(Graph, TEXT("bDoorMoving"), true, 80, -220);
	UK2Node_CallFunction* OpenPlay = AddPlay(OpenId, -300, -60);
	UK2Node_VariableSet* SetOpen = AddBoolSet(Graph, TEXT("bDoorOpen"), true, -80, -60);
	UK2Node_VariableSet* StartOpening = AddBoolSet(Graph, TEXT("bDoorMoving"), true, 80, -60);
	ConnectPins(FindPinChecked(ToggleInput, TEXT("Pressed")), ToggleMoving->GetExecPin());
	ConnectPins(ToggleMovingGet->GetValuePin(), ToggleMoving->GetConditionPin());
	ConnectPins(ToggleMoving->GetElsePin(), ToggleLocked->GetExecPin());
	ConnectPins(ToggleLockedGet->GetValuePin(), ToggleLocked->GetConditionPin());
	ConnectPins(ToggleLocked->GetThenPin(), ToggleRattle->GetExecPin());
	ConnectPins(ToggleLocked->GetElsePin(), ToggleOpen->GetExecPin());
	ConnectPins(ToggleOpenGet->GetValuePin(), ToggleOpen->GetConditionPin());
	ConnectPins(ToggleOpen->GetThenPin(), ClosePlay->GetExecPin());
	ConnectPins(ClosePlay->GetThenPin(), SetClosed->GetExecPin());
	ConnectPins(SetClosed->GetThenPin(), StartClosing->GetExecPin());
	ConnectPins(StartClosing->GetThenPin(), DoorClose->GetPlayFromStartPin());
	ConnectPins(ToggleOpen->GetElsePin(), OpenPlay->GetExecPin());
	ConnectPins(OpenPlay->GetThenPin(), SetOpen->GetExecPin());
	ConnectPins(SetOpen->GetThenPin(), StartOpening->GetExecPin());
	ConnectPins(StartOpening->GetThenPin(), DoorOpen->GetPlayFromStartPin());

	// G: slam only while open; it uses the same closed state as a normal close.
	UK2Node_InputKey* SlamInput = AddKeyEvent(Graph, EKeys::G, -1250, 40);
	UK2Node_IfThenElse* SlamMoving = AddBranch(Graph, -1040, 40);
	UK2Node_VariableGet* SlamMovingGet = AddSelfVariableGet(Graph, TEXT("bDoorMoving"), -1040, 130);
	UK2Node_IfThenElse* SlamLocked = AddBranch(Graph, -800, 40);
	UK2Node_VariableGet* SlamLockedGet = AddSelfVariableGet(Graph, TEXT("bDoorLocked"), -800, 130);
	UK2Node_IfThenElse* SlamOpen = AddBranch(Graph, -540, 40);
	UK2Node_VariableGet* SlamOpenGet = AddSelfVariableGet(Graph, TEXT("bDoorOpen"), -540, 130);
	UK2Node_CallFunction* SlamRattle = AddRattle(-300, 150);
	UK2Node_CallFunction* SlamPlay = AddPlay(SlamId, -300, 40);
	UK2Node_VariableSet* SlamClosed = AddBoolSet(Graph, TEXT("bDoorOpen"), false, -80, 40);
	UK2Node_VariableSet* StartSlam = AddBoolSet(Graph, TEXT("bDoorMoving"), true, 80, 40);
	ConnectPins(FindPinChecked(SlamInput, TEXT("Pressed")), SlamMoving->GetExecPin());
	ConnectPins(SlamMovingGet->GetValuePin(), SlamMoving->GetConditionPin());
	ConnectPins(SlamMoving->GetElsePin(), SlamLocked->GetExecPin());
	ConnectPins(SlamLockedGet->GetValuePin(), SlamLocked->GetConditionPin());
	ConnectPins(SlamLocked->GetThenPin(), SlamRattle->GetExecPin());
	ConnectPins(SlamLocked->GetElsePin(), SlamOpen->GetExecPin());
	ConnectPins(SlamOpenGet->GetValuePin(), SlamOpen->GetConditionPin());
	ConnectPins(SlamOpen->GetThenPin(), SlamPlay->GetExecPin());
	ConnectPins(SlamPlay->GetThenPin(), SlamClosed->GetExecPin());
	ConnectPins(SlamClosed->GetThenPin(), StartSlam->GetExecPin());
	ConnectPins(StartSlam->GetThenPin(), DoorSlam->GetPlayFromStartPin());

	// L: lock/unlock only while the leaf is closed; the active transition is a no-op.
	UK2Node_InputKey* LockInput = AddKeyEvent(Graph, EKeys::L, -1250, 300);
	UK2Node_IfThenElse* LockMoving = AddBranch(Graph, -1040, 300);
	UK2Node_VariableGet* LockMovingGet = AddSelfVariableGet(Graph, TEXT("bDoorMoving"), -1040, 390);
	UK2Node_IfThenElse* LockOpen = AddBranch(Graph, -800, 300);
	UK2Node_VariableGet* LockOpenGet = AddSelfVariableGet(Graph, TEXT("bDoorOpen"), -800, 390);
	UK2Node_IfThenElse* LockLocked = AddBranch(Graph, -540, 300);
	UK2Node_VariableGet* LockLockedGet = AddSelfVariableGet(Graph, TEXT("bDoorLocked"), -540, 390);
	UK2Node_CallFunction* UnlockPlay = AddPlay(UnlockId, -300, 390);
	UK2Node_VariableSet* SetUnlocked = AddBoolSet(Graph, TEXT("bDoorLocked"), false, -80, 390);
	UK2Node_CallFunction* LockPlay = AddPlay(LockId, -300, 300);
	UK2Node_VariableSet* SetLocked = AddBoolSet(Graph, TEXT("bDoorLocked"), true, -80, 300);
	ConnectPins(FindPinChecked(LockInput, TEXT("Pressed")), LockMoving->GetExecPin());
	ConnectPins(LockMovingGet->GetValuePin(), LockMoving->GetConditionPin());
	ConnectPins(LockMoving->GetElsePin(), LockOpen->GetExecPin());
	ConnectPins(LockOpenGet->GetValuePin(), LockOpen->GetConditionPin());
	ConnectPins(LockOpen->GetElsePin(), LockLocked->GetExecPin());
	ConnectPins(LockLockedGet->GetValuePin(), LockLocked->GetConditionPin());
	ConnectPins(LockLocked->GetThenPin(), UnlockPlay->GetExecPin());
	ConnectPins(UnlockPlay->GetThenPin(), SetUnlocked->GetExecPin());
	ConnectPins(LockLocked->GetElsePin(), LockPlay->GetExecPin());
	ConnectPins(LockPlay->GetThenPin(), SetLocked->GetExecPin());

	SetMetadata(Blueprint, 2, TEXT("Door"), FVector(-400.0f, 20.0f, 0.0f),
		{ { FText::FromString(TEXT("F")), FText::FromString(TEXT("open / close; rattle while locked")) },
		  { FText::FromString(TEXT("G")), FText::FromString(TEXT("slam while open; rattle while locked")) },
		  { FText::FromString(TEXT("L")), FText::FromString(TEXT("lock / unlock while closed")) } });
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

void ReplaceZoneActor(UWorld* World, const TCHAR* Label, UClass* ActorClass, const FVector& DefaultLocation)
{
	check(ActorClass != nullptr);
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
	AActor* Replacement = World->SpawnActor<AActor>(ActorClass, Transform, Params);
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
	check(Door != nullptr);
	CreateDoorBlueprint(Door, EventMap);
	FKismetEditorUtilities::CompileBlueprint(Door);
	FAssetRegistryModule::AssetCreated(Door);
	Door->MarkPackageDirty();
	UPackage::SavePackage(Door->GetOutermost(), Door, *FPackageName::LongPackageNameToFilename(Door->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension()), FSavePackageArgs());

	UWorld* World = GEditor->GetEditorWorldContext().World();
	checkf(World != nullptr && World->GetOutermost()->GetName() == MapPath, TEXT("Open the Showcase map before running Hapbeat.GenerateBlueprintShowcase."));
	ReplaceZoneActor(World, TEXT("Z2_Door"), Door->GeneratedClass, FVector(0.0f, 3000.0f, 0.0f));
	ReplaceZoneActor(World, TEXT("Z4_StreamConsole"), AHapbeatShowcaseZ4StreamConsoleActor::StaticClass(), FVector(0.0f, 9000.0f, 0.0f));
	FEditorFileUtils::SaveLevel(World->PersistentLevel);
	UE_LOG(LogTemp, Display, TEXT("[Hapbeat] Generated BP_Z2_Door and restored the C++ Z4 Stream Console in the Showcase map."));
}

void GenerateDoorAsset()
{
	UHapbeatEventMap* EventMap = GetShowcaseEventMap();
	checkf(EventMap != nullptr, TEXT("Could not load EM_Showcase."));
	UBlueprint* Door = LoadOrCreateBlueprint(TEXT("BP_Z2_Door"));
	check(Door != nullptr);
	CreateDoorBlueprint(Door, EventMap);
	FKismetEditorUtilities::CompileBlueprint(Door);
	FAssetRegistryModule::AssetCreated(Door);
	Door->MarkPackageDirty();
	UPackage::SavePackage(Door->GetOutermost(), Door,
		*FPackageName::LongPackageNameToFilename(Door->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension()),
		FSavePackageArgs());
	UE_LOG(LogTemp, Display, TEXT("[Hapbeat] Generated BP_Z2_Door without changing the Showcase map."));
}

}
