// Copyright (c) 2026 Hapbeat. MIT License.

/** Editor-only authoring command for the two Blueprint-complete Showcase zones. */

#include "HapbeatShowcaseBlueprintBuilder.h"

#include "HapbeatShowcaseBlueprintZoneActor.h"
#include "HapbeatShowcaseZ4ConsoleWidget.h"

#include "HapbeatAddressOverridePanelComponent.h"
#include "HapbeatBlueprintLibrary.h"
#include "HapbeatEventMap.h"
#include "HapbeatParameterBinding.h"
#include "HapbeatStreamPlayback.h"
#include "HapbeatTriggerComponent.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Curves/CurveFloat.h"
#include "Editor.h"
#include "EdGraphNode_Comment.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/StaticMesh.h"
#include "Engine/TimelineTemplate.h"
#include "EngineUtils.h"
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "FileHelpers.h"
#include "InputCoreTypes.h"
#include "K2Node_CallFunction.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_Event.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_InputKey.h"
#include "K2Node_SwitchEnum.h"
#include "K2Node_Timeline.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Materials/MaterialInterface.h"
#include "Sound/SoundBase.h"
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

UBlueprint* LoadOrCreateBlueprint(const TCHAR* AssetName, bool& bWasCreated)
{
	bWasCreated = false;
	const FString ObjectPath = FString::Printf(TEXT("%s/%s.%s"), AssetFolder, AssetName, AssetName);
	if (UBlueprint* Existing = LoadObject<UBlueprint>(nullptr, *ObjectPath))
	{
		return Existing;
	}

	const FString PackageName = FString::Printf(TEXT("%s/%s"), AssetFolder, AssetName);
	UPackage* Package = CreatePackage(*PackageName);
	bWasCreated = true;
	return FKismetEditorUtilities::CreateBlueprint(
		AHapbeatShowcaseBlueprintZoneActor::StaticClass(), Package, FName(AssetName),
		BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass(),
		FName(TEXT("HapbeatShowcaseBlueprintBuilder")));
}

UWidgetBlueprint* LoadOrCreateWidgetBlueprint(const TCHAR* AssetName, bool& bWasCreated)
{
	bWasCreated = false;
	const FString ObjectPath = FString::Printf(TEXT("%s/%s.%s"), AssetFolder, AssetName, AssetName);
	if (UWidgetBlueprint* Existing = LoadObject<UWidgetBlueprint>(nullptr, *ObjectPath))
	{
		return Existing;
	}

	const FString PackageName = FString::Printf(TEXT("%s/%s"), AssetFolder, AssetName);
	UPackage* Package = CreatePackage(*PackageName);
	bWasCreated = true;
	return CastChecked<UWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
		UHapbeatShowcaseZ4ConsoleWidget::StaticClass(), Package, FName(AssetName),
		BPTYPE_Normal, UWidgetBlueprint::StaticClass(), UWidgetBlueprintGeneratedClass::StaticClass(),
		FName(TEXT("HapbeatShowcaseBlueprintBuilder"))));
}

void ClearGeneratedGraph(UBlueprint* Blueprint)
{
	// These are state variables owned by the generated Z2 graph.  Remove them
	// before rebuilding so the command is idempotent and their defaults cannot
	// drift from the graph it creates.
	for (const FName VariableName : { FName(TEXT("DoorState")), FName(TEXT("bDoorOpen")), FName(TEXT("bDoorLocked")), FName(TEXT("bDoorMoving")), FName(TEXT("ConsoleWidget")) })
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

UK2Node_VariableSet* AddSelfVariableSet(UEdGraph* Graph, const TCHAR* VariableName, int32 X, int32 Y)
{
	UK2Node_VariableSet* Node = AddNode<UK2Node_VariableSet>(Graph, X, Y);
	Node->VariableReference.SetSelfMember(FName(VariableName));
	Node->ReconstructNode();
	return Node;
}

UK2Node_Event* AddOverrideEvent(UEdGraph* Graph, UClass* OwnerClass, FName Function, int32 X, int32 Y)
{
	UK2Node_Event* Node = NewObject<UK2Node_Event>(Graph);
	Node->EventReference.SetExternalMember(Function, OwnerClass);
	Node->bOverrideFunction = true;
	Graph->AddNode(Node, false, false);
	Node->NodePosX = X;
	Node->NodePosY = Y;
	Node->CreateNewGuid();
	Node->PostPlacedNewNode();
	Node->AllocateDefaultPins();
	return Node;
}

void AddObjectMember(UBlueprint* Blueprint, const TCHAR* Name, UClass* Class)
{
	FEdGraphPinType Type;
	Type.PinCategory = UEdGraphSchema_K2::PC_Object;
	Type.PinSubCategoryObject = Class;
	checkf(FBlueprintEditorUtils::AddMemberVariable(Blueprint, FName(Name), Type, FString()),
		TEXT("Could not add generated object variable '%s'."), Name);
}

void SaveBlueprintAsset(UBlueprint* Blueprint)
{
	check(Blueprint != nullptr);
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	FAssetRegistryModule::AssetCreated(Blueprint);
	Blueprint->MarkPackageDirty();
	UPackage::SavePackage(Blueprint->GetOutermost(), Blueprint,
		*FPackageName::LongPackageNameToFilename(Blueprint->GetOutermost()->GetName(),
			FPackageName::GetAssetPackageExtension()), FSavePackageArgs());
}

UK2Node_SwitchEnum* AddDoorStateSwitch(UEdGraph* Graph, int32 X, int32 Y)
{
	UK2Node_SwitchEnum* Node = AddNode<UK2Node_SwitchEnum>(Graph, X, Y);
	// UK2Node_SwitchEnum::SetEnum is not exported from BlueprintGraph. Assigning
	// the public enum field then reconstructing is the same initialization path:
	// CreateCasePins() refreshes the case list from this enum.
	Node->Enum = StaticEnum<EHapbeatShowcaseDoorState>();
	Node->ReconstructNode();
	return Node;
}

void AddComment(UEdGraph* Graph, const TCHAR* Title, int32 X, int32 Y, int32 Width, int32 Height,
	const FLinearColor& Color)
{
	UEdGraphNode_Comment* Node = AddNode<UEdGraphNode_Comment>(Graph, X, Y);
	Node->NodeComment = Title;
	Node->NodeWidth = Width;
	Node->NodeHeight = Height;
	Node->CommentColor = Color;
	Node->MoveMode = ECommentBoxMode::NoGroupMovement;
}

void AddDoorStateMember(UBlueprint* Blueprint)
{
	FEdGraphPinType EnumType;
	EnumType.PinCategory = UEdGraphSchema_K2::PC_Byte;
	EnumType.PinSubCategoryObject = StaticEnum<EHapbeatShowcaseDoorState>();
	checkf(FBlueprintEditorUtils::AddMemberVariable(Blueprint, TEXT("DoorState"), EnumType, TEXT("Closed")),
		TEXT("Could not add generated DoorState enum."));
}

UK2Node_VariableSet* AddDoorStateSet(UEdGraph* Graph, EHapbeatShowcaseDoorState Value, int32 X, int32 Y)
{
	UK2Node_VariableSet* Node = AddNode<UK2Node_VariableSet>(Graph, X, Y);
	Node->VariableReference.SetSelfMember(TEXT("DoorState"));
	Node->ReconstructNode();
	UEdGraphPin* ValuePin = Node->FindPin(TEXT("DoorState"), EGPD_Input);
	check(ValuePin != nullptr);
	ValuePin->DefaultValue = StaticEnum<EHapbeatShowcaseDoorState>()->GetNameStringByValue(static_cast<int64>(Value));
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
	USoundBase* OpenSound = LoadObject<USoundBase>(nullptr,
		TEXT("/HapbeatSDK/HapbeatSamples/Showcase/Sounds/S_z2_door_open.S_z2_door_open"));
	USoundBase* CloseSound = LoadObject<USoundBase>(nullptr,
		TEXT("/HapbeatSDK/HapbeatSamples/Showcase/Sounds/S_z2_door_close.S_z2_door_close"));
	USoundBase* SlamSound = LoadObject<USoundBase>(nullptr,
		TEXT("/HapbeatSDK/HapbeatSamples/Showcase/Sounds/S_z2_door_slam.S_z2_door_slam"));
	USoundBase* LockSound = LoadObject<USoundBase>(nullptr,
		TEXT("/HapbeatSDK/HapbeatSamples/Showcase/Sounds/S_z2_door_lock.S_z2_door_lock"));
	USoundBase* UnlockSound = LoadObject<USoundBase>(nullptr,
		TEXT("/HapbeatSDK/HapbeatSamples/Showcase/Sounds/S_z2_door_unlock.S_z2_door_unlock"));
	USoundBase* RattleSound = LoadObject<USoundBase>(nullptr,
		TEXT("/HapbeatSDK/HapbeatSamples/Showcase/Sounds/S_z2_door_rattle.S_z2_door_rattle"));
	AddDoorStateMember(Blueprint);

	// Z2 is deliberately a Blueprint-complete example.  The three motion
	// timelines preserve the prior C++ timings: open (2.0 s), normal close
	// (2.2 s), and slam (0.117 s).  A separate rattle timeline keeps the locked
	// feedback readable in the graph instead of hiding it in native code.
	UTimelineTemplate* OpenTemplate = CreateDoorMotionTimeline(Blueprint, TEXT("DoorOpen"), 2.0f, 0.0f, 1.0f);
	UTimelineTemplate* CloseTemplate = CreateDoorMotionTimeline(Blueprint, TEXT("DoorClose"), 2.2f, 1.0f, 0.0f);
	UTimelineTemplate* SlamTemplate = CreateDoorMotionTimeline(Blueprint, TEXT("DoorSlam"), 0.117f, 1.0f, 0.0f);
	UTimelineTemplate* RattleTemplate = CreateDoorRattleTimeline(Blueprint);
	// Each action occupies one horizontal lane. The input paths use one enum and
	// Switch on Door State rather than three interdependent bool branches.
	AddComment(Graph, TEXT("OPEN  |  z2_door_open  ->  Play Hapbeat Event  ->  Play Sound 2D"), -360, -585, 2080, 190,
		FLinearColor(0.10f, 0.42f, 0.22f));
	AddComment(Graph, TEXT("CLOSE  |  z2_door_close  ->  Play Hapbeat Event  ->  Play Sound 2D"), -360, -335, 2080, 190,
		FLinearColor(0.12f, 0.30f, 0.52f));
	AddComment(Graph, TEXT("SLAM  |  z2_door_slam  ->  Play Hapbeat Event  ->  Play Sound 2D"), -360, -85, 2080, 190,
		FLinearColor(0.62f, 0.25f, 0.08f));
	AddComment(Graph, TEXT("LOCKED RATTLE  |  z2_door_rattle  ->  Play Hapbeat Event  ->  Play Sound 2D"), -360, 165, 2080, 220,
		FLinearColor(0.50f, 0.15f, 0.15f));
	AddComment(Graph, TEXT("LOCK / UNLOCK  |  z2_door_lock / z2_door_unlock  ->  Play Hapbeat Event  ->  Play Sound 2D"), -360, 445, 1180, 300,
		FLinearColor(0.35f, 0.25f, 0.58f));
	AddComment(Graph, TEXT("F  |  Switch on Door State"), -1390, -485, 710, 250,
		FLinearColor(0.18f, 0.18f, 0.18f));
	AddComment(Graph, TEXT("G  |  Switch on Door State"), -1390, -35, 710, 220,
		FLinearColor(0.18f, 0.18f, 0.18f));
	AddComment(Graph, TEXT("L  |  Switch on Door State"), -1390, 430, 710, 250,
		FLinearColor(0.18f, 0.18f, 0.18f));

	UK2Node_Timeline* DoorOpen = AddTimeline(Graph, OpenTemplate, 500, -500);
	UK2Node_Timeline* DoorClose = AddTimeline(Graph, CloseTemplate, 500, -250);
	UK2Node_Timeline* DoorSlam = AddTimeline(Graph, SlamTemplate, 500, 0);
	UK2Node_Timeline* DoorRattle = AddTimeline(Graph, RattleTemplate, 500, 250);

	auto AddMotionRotation = [&](UK2Node_Timeline* Timeline, int32 Y)
	{
		UK2Node_CallFunction* LerpRotation = AddCall(Graph, UKismetMathLibrary::StaticClass(),
			GET_FUNCTION_NAME_CHECKED(UKismetMathLibrary, RLerp), 800, Y);
		FindPinChecked(LerpRotation, TEXT("A"))->DefaultValue = TEXT("0.000000,-90.000000,0.000000");
		FindPinChecked(LerpRotation, TEXT("B"))->DefaultValue = TEXT("0.000000,0.000000,0.000000");
		FindPinChecked(LerpRotation, TEXT("bShortestPath"))->DefaultValue = TEXT("false");
		UK2Node_CallFunction* SetRotation = AddCall(Graph, USceneComponent::StaticClass(),
			GET_FUNCTION_NAME_CHECKED(USceneComponent, K2_SetRelativeRotation), 1100, Y);
		UK2Node_VariableGet* HingeGet = AddComponentGet(Graph, TEXT("DoorHinge"), 980, Y + 100);
		ConnectPins(Timeline->GetUpdatePin(), SetRotation->GetExecPin());
		ConnectPins(FindPinChecked(Timeline, TEXT("OpenAlpha")), FindPinChecked(LerpRotation, TEXT("Alpha")));
		ConnectPins(LerpRotation->GetReturnValuePin(), FindPinChecked(SetRotation, TEXT("NewRotation")));
		ConnectPins(FindPinChecked(HingeGet, TEXT("DoorHinge")), FindTargetPinChecked(SetRotation));
	};
	AddMotionRotation(DoorOpen, -500);
	AddMotionRotation(DoorClose, -250);
	AddMotionRotation(DoorSlam, 0);

	UK2Node_CallFunction* MakeRattleRotation = AddCall(Graph, UKismetMathLibrary::StaticClass(),
		GET_FUNCTION_NAME_CHECKED(UKismetMathLibrary, MakeRotator), 920, 250);
	UK2Node_CallFunction* AddRattleYaw = AddCall(Graph, UKismetMathLibrary::StaticClass(),
		GET_FUNCTION_NAME_CHECKED(UKismetMathLibrary, Add_FloatFloat), 760, 250);
	UK2Node_CallFunction* SetRattleRotation = AddCall(Graph, USceneComponent::StaticClass(),
		GET_FUNCTION_NAME_CHECKED(USceneComponent, K2_SetRelativeRotation), 1220, 250);
	FindPinChecked(AddRattleYaw, TEXT("A"))->DefaultValue = TEXT("-90.000000");
	FindPinChecked(MakeRattleRotation, TEXT("Pitch"))->DefaultValue = TEXT("0.000000");
	FindPinChecked(MakeRattleRotation, TEXT("Roll"))->DefaultValue = TEXT("0.000000");
	ConnectPins(DoorRattle->GetUpdatePin(), SetRattleRotation->GetExecPin());
	ConnectPins(FindPinChecked(DoorRattle, TEXT("RattleYaw")), FindPinChecked(AddRattleYaw, TEXT("B")));
	ConnectPins(AddRattleYaw->GetReturnValuePin(), FindPinChecked(MakeRattleRotation, TEXT("Yaw")));
	ConnectPins(MakeRattleRotation->GetReturnValuePin(), FindPinChecked(SetRattleRotation, TEXT("NewRotation")));
	UK2Node_VariableGet* RattleHingeGet = AddComponentGet(Graph, TEXT("DoorHinge"), 1100, 350);
	ConnectPins(FindPinChecked(RattleHingeGet, TEXT("DoorHinge")), FindTargetPinChecked(SetRattleRotation));

	// Moving input is ignored, matching the previous C++ state machine.  Each
	// terminal timeline clears the guard when it reaches its closed/open state.
	UK2Node_VariableSet* StopOpening = AddDoorStateSet(Graph, EHapbeatShowcaseDoorState::Open, 1420, -500);
	UK2Node_VariableSet* StopClosing = AddDoorStateSet(Graph, EHapbeatShowcaseDoorState::Closed, 1420, -250);
	UK2Node_VariableSet* StopSlamming = AddDoorStateSet(Graph, EHapbeatShowcaseDoorState::Closed, 1420, 0);
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
	struct FDoorAction
	{
		UK2Node_CallFunction* Entry = nullptr;
		UK2Node_CallFunction* Exit = nullptr;
	};
	auto AddAction = [&](const FGuid& EventId, USoundBase* Sound, int32 X, int32 Y)
	{
		UK2Node_CallFunction* HapticPlay = AddPlay(EventId, X, Y);
		UK2Node_CallFunction* SoundPlay = AddCall(Graph, UGameplayStatics::StaticClass(),
			GET_FUNCTION_NAME_CHECKED(UGameplayStatics, PlaySound2D), X + 300, Y);
		FindPinChecked(SoundPlay, TEXT("Sound"))->DefaultObject = Sound;
		ConnectPins(HapticPlay->GetThenPin(), SoundPlay->GetExecPin());
		return FDoorAction { HapticPlay, SoundPlay };
	};
	auto AddRattle = [&](int32 X, int32 Y)
	{
		FDoorAction RattleAction = AddAction(RattleId, RattleSound, X, Y);
		ConnectPins(RattleAction.Exit->GetThenPin(), DoorRattle->GetPlayFromStartPin());
		return RattleAction;
	};

	// F: open when closed, close when open, and rattle while locked.
	UK2Node_InputKey* ToggleInput = AddKeyEvent(Graph, EKeys::F, -1350, -400);
	UK2Node_SwitchEnum* ToggleState = AddDoorStateSwitch(Graph, -1000, -400);
	UK2Node_VariableGet* ToggleStateGet = AddSelfVariableGet(Graph, TEXT("DoorState"), -1000, -300);
	FDoorAction ToggleRattle = AddRattle(-300, 200);
	FDoorAction ClosePlay = AddAction(CloseId, CloseSound, -300, -250);
	UK2Node_VariableSet* StartClosing = AddDoorStateSet(Graph, EHapbeatShowcaseDoorState::Closing, 260, -250);
	FDoorAction OpenPlay = AddAction(OpenId, OpenSound, -300, -500);
	UK2Node_VariableSet* StartOpening = AddDoorStateSet(Graph, EHapbeatShowcaseDoorState::Opening, 260, -500);
	ConnectPins(FindPinChecked(ToggleInput, TEXT("Pressed")), ToggleState->GetExecPin());
	ConnectPins(ToggleStateGet->GetValuePin(), FindPinChecked(ToggleState, TEXT("Selection")));
	ConnectPins(FindPinChecked(ToggleState, TEXT("Locked")), ToggleRattle.Entry->GetExecPin());
	ConnectPins(FindPinChecked(ToggleState, TEXT("Open")), ClosePlay.Entry->GetExecPin());
	ConnectPins(ClosePlay.Exit->GetThenPin(), StartClosing->GetExecPin());
	ConnectPins(StartClosing->GetThenPin(), DoorClose->GetPlayFromStartPin());
	ConnectPins(FindPinChecked(ToggleState, TEXT("Closed")), OpenPlay.Entry->GetExecPin());
	ConnectPins(OpenPlay.Exit->GetThenPin(), StartOpening->GetExecPin());
	ConnectPins(StartOpening->GetThenPin(), DoorOpen->GetPlayFromStartPin());

	// G: slam only while open; it uses the same closed state as a normal close.
	UK2Node_InputKey* SlamInput = AddKeyEvent(Graph, EKeys::G, -1350, 30);
	UK2Node_SwitchEnum* SlamState = AddDoorStateSwitch(Graph, -1000, 30);
	UK2Node_VariableGet* SlamStateGet = AddSelfVariableGet(Graph, TEXT("DoorState"), -1000, 130);
	FDoorAction SlamRattle = AddRattle(-300, 300);
	FDoorAction SlamPlay = AddAction(SlamId, SlamSound, -300, 0);
	UK2Node_VariableSet* StartSlam = AddDoorStateSet(Graph, EHapbeatShowcaseDoorState::Slamming, 260, 0);
	ConnectPins(FindPinChecked(SlamInput, TEXT("Pressed")), SlamState->GetExecPin());
	ConnectPins(SlamStateGet->GetValuePin(), FindPinChecked(SlamState, TEXT("Selection")));
	ConnectPins(FindPinChecked(SlamState, TEXT("Locked")), SlamRattle.Entry->GetExecPin());
	ConnectPins(FindPinChecked(SlamState, TEXT("Open")), SlamPlay.Entry->GetExecPin());
	ConnectPins(SlamPlay.Exit->GetThenPin(), StartSlam->GetExecPin());
	ConnectPins(StartSlam->GetThenPin(), DoorSlam->GetPlayFromStartPin());

	// L: lock/unlock only while the leaf is closed; the active transition is a no-op.
	UK2Node_InputKey* LockInput = AddKeyEvent(Graph, EKeys::L, -1350, 500);
	UK2Node_SwitchEnum* LockState = AddDoorStateSwitch(Graph, -1000, 500);
	UK2Node_VariableGet* LockStateGet = AddSelfVariableGet(Graph, TEXT("DoorState"), -1000, 600);
	FDoorAction UnlockPlay = AddAction(UnlockId, UnlockSound, -300, 650);
	UK2Node_VariableSet* SetUnlocked = AddDoorStateSet(Graph, EHapbeatShowcaseDoorState::Closed, 260, 650);
	FDoorAction LockPlay = AddAction(LockId, LockSound, -300, 500);
	UK2Node_VariableSet* SetLocked = AddDoorStateSet(Graph, EHapbeatShowcaseDoorState::Locked, 260, 500);
	ConnectPins(FindPinChecked(LockInput, TEXT("Pressed")), LockState->GetExecPin());
	ConnectPins(LockStateGet->GetValuePin(), FindPinChecked(LockState, TEXT("Selection")));
	ConnectPins(FindPinChecked(LockState, TEXT("Locked")), UnlockPlay.Entry->GetExecPin());
	ConnectPins(UnlockPlay.Exit->GetThenPin(), SetUnlocked->GetExecPin());
	ConnectPins(FindPinChecked(LockState, TEXT("Closed")), LockPlay.Entry->GetExecPin());
	ConnectPins(LockPlay.Exit->GetThenPin(), SetLocked->GetExecPin());

	for (const TPair<const TCHAR*, FDoorAction> Action : {
		TPair<const TCHAR*, FDoorAction>(TEXT("open"), OpenPlay),
		TPair<const TCHAR*, FDoorAction>(TEXT("close"), ClosePlay),
		TPair<const TCHAR*, FDoorAction>(TEXT("slam"), SlamPlay),
		TPair<const TCHAR*, FDoorAction>(TEXT("toggle rattle"), ToggleRattle),
		TPair<const TCHAR*, FDoorAction>(TEXT("slam rattle"), SlamRattle),
		TPair<const TCHAR*, FDoorAction>(TEXT("lock"), LockPlay),
		TPair<const TCHAR*, FDoorAction>(TEXT("unlock"), UnlockPlay) })
	{
		checkf(Action.Value.Entry->GetExecPin()->LinkedTo.Num() > 0,
			TEXT("Z2 %s action must enter through Play Hapbeat Event."), Action.Key);
		checkf(Action.Value.Entry->GetThenPin()->LinkedTo.Contains(Action.Value.Exit->GetExecPin()),
			TEXT("Z2 %s action must run Play Sound 2D after Play Hapbeat Event."), Action.Key);
	}

	SetMetadata(Blueprint, 2, TEXT("Door"), FVector(-400.0f, 20.0f, 0.0f),
		{ { FText::FromString(TEXT("F")), FText::FromString(TEXT("open / close; rattle while locked")) },
		  { FText::FromString(TEXT("G")), FText::FromString(TEXT("slam while open; rattle while locked")) },
		  { FText::FromString(TEXT("L")), FText::FromString(TEXT("lock / unlock while closed")) } });
}

void CreateStreamConsoleWidgetBlueprint(UWidgetBlueprint* Blueprint)
{
	ClearGeneratedGraph(Blueprint);
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	UEdGraph* Graph = GetEventGraph(Blueprint);

	AddComment(Graph, TEXT("GAIN SLIDER  |  Set Value  ->  Evaluate Now (Gain Binding)"), -680, -310, 1720, 180,
		FLinearColor(0.10f, 0.42f, 0.22f));
	AddComment(Graph, TEXT("PAN SLIDER  |  Set Value  ->  Evaluate Now (Pan Binding)"), -680, -60, 1720, 180,
		FLinearColor(0.12f, 0.30f, 0.52f));
	AddComment(Graph, TEXT("DETENT TICK  |  Play Sound 2D  ->  Fire (Tick Trigger)"), -680, 190, 1680, 180,
		FLinearColor(0.62f, 0.25f, 0.08f));

	UK2Node_Event* GainEvent = AddOverrideEvent(Graph, UHapbeatShowcaseZ4ConsoleWidget::StaticClass(),
		GET_FUNCTION_NAME_CHECKED(UHapbeatShowcaseZ4ConsoleWidget, HandleGainValueChanged), -600, -260);
	UK2Node_VariableGet* GainBinding = AddSelfVariableGet(Graph, TEXT("GainBinding"), -360, -160);
	UK2Node_CallFunction* SetGain = AddCall(Graph, UHapbeatParameterBinding::StaticClass(),
		GET_FUNCTION_NAME_CHECKED(UHapbeatParameterBinding, SetValue), 0, -260);
	UK2Node_CallFunction* EvaluateGain = AddCall(Graph, UHapbeatParameterBinding::StaticClass(),
		GET_FUNCTION_NAME_CHECKED(UHapbeatParameterBinding, EvaluateNow), 280, -260);
	ConnectPins(GainEvent->GetThenPin(), SetGain->GetExecPin());
	ConnectPins(GainBinding->GetValuePin(), FindTargetPinChecked(SetGain));
	ConnectPins(FindPinChecked(GainEvent, TEXT("Value")), FindPinChecked(SetGain, TEXT("Value")));
	ConnectPins(SetGain->GetThenPin(), EvaluateGain->GetExecPin());
	ConnectPins(GainBinding->GetValuePin(), FindTargetPinChecked(EvaluateGain));

	UK2Node_Event* PanEvent = AddOverrideEvent(Graph, UHapbeatShowcaseZ4ConsoleWidget::StaticClass(),
		GET_FUNCTION_NAME_CHECKED(UHapbeatShowcaseZ4ConsoleWidget, HandlePanValueChanged), -600, -10);
	UK2Node_VariableGet* PanBinding = AddSelfVariableGet(Graph, TEXT("PanBinding"), -360, 90);
	UK2Node_CallFunction* SetPan = AddCall(Graph, UHapbeatParameterBinding::StaticClass(),
		GET_FUNCTION_NAME_CHECKED(UHapbeatParameterBinding, SetValue), 0, -10);
	UK2Node_CallFunction* EvaluatePan = AddCall(Graph, UHapbeatParameterBinding::StaticClass(),
		GET_FUNCTION_NAME_CHECKED(UHapbeatParameterBinding, EvaluateNow), 280, -10);
	ConnectPins(PanEvent->GetThenPin(), SetPan->GetExecPin());
	ConnectPins(PanBinding->GetValuePin(), FindTargetPinChecked(SetPan));
	ConnectPins(FindPinChecked(PanEvent, TEXT("Value")), FindPinChecked(SetPan, TEXT("Value")));
	ConnectPins(SetPan->GetThenPin(), EvaluatePan->GetExecPin());
	ConnectPins(PanBinding->GetValuePin(), FindTargetPinChecked(EvaluatePan));

	UK2Node_Event* TickEvent = AddOverrideEvent(Graph, UHapbeatShowcaseZ4ConsoleWidget::StaticClass(),
		GET_FUNCTION_NAME_CHECKED(UHapbeatShowcaseZ4ConsoleWidget, HandleTick), -600, 240);
	UK2Node_VariableGet* TickSound = AddSelfVariableGet(Graph, TEXT("TickSound"), -380, 360);
	UK2Node_CallFunction* PlaySound = AddCall(Graph, UGameplayStatics::StaticClass(),
		GET_FUNCTION_NAME_CHECKED(UGameplayStatics, PlaySound2D), -120, 240);
	UK2Node_VariableGet* TickTrigger = AddSelfVariableGet(Graph, TEXT("TickTrigger"), 120, 360);
	UK2Node_CallFunction* FireTick = AddCall(Graph, UHapbeatTriggerComponent::StaticClass(),
		GET_FUNCTION_NAME_CHECKED(UHapbeatTriggerComponent, Fire), 380, 240);
	ConnectPins(TickEvent->GetThenPin(), PlaySound->GetExecPin());
	ConnectPins(TickSound->GetValuePin(), FindPinChecked(PlaySound, TEXT("Sound")));
	ConnectPins(PlaySound->GetThenPin(), FireTick->GetExecPin());
	ConnectPins(TickTrigger->GetValuePin(), FindTargetPinChecked(FireTick));
}

void ConfigureStreamBindingTargets(UBlueprint* Blueprint)
{
	UEdGraph* Construction = nullptr;
	for (UEdGraph* Candidate : Blueprint->FunctionGraphs)
	{
		if (Candidate->GetFName() == UEdGraphSchema_K2::FN_UserConstructionScript)
		{
			Construction = Candidate;
			break;
		}
	}
	check(Construction != nullptr);
	UK2Node_FunctionEntry* Entry = nullptr;
	const auto OldNodes = Construction->Nodes;
	for (UEdGraphNode* Node : OldNodes)
	{
		if (auto* FunctionEntry = Cast<UK2Node_FunctionEntry>(Node)) { Entry = FunctionEntry; }
		else { FBlueprintEditorUtils::RemoveNode(Blueprint, Node, true); }
	}
	check(Entry != nullptr);
	UEdGraphPin* Previous = FindPinChecked(Entry, TEXT("then"));
	Previous->BreakAllPinLinks();
	int32 X = 320;
	for (const TCHAR* Name : { TEXT("GainBinding"), TEXT("PanBinding") })
	{
		auto* Binding = AddComponentGet(Construction, Name, X, 160);
		auto* Loop = AddComponentGet(Construction, TEXT("LoopTrigger"), X, 240);
		auto* SetTarget = AddNode<UK2Node_VariableSet>(Construction, X + 240, 0);
		SetTarget->VariableReference.SetExternalMember(TEXT("TargetTrigger"), UHapbeatParameterBinding::StaticClass());
		SetTarget->ReconstructNode();
		ConnectPins(Previous, SetTarget->GetExecPin());
		ConnectPins(Binding->GetValuePin(), FindTargetPinChecked(SetTarget));
		ConnectPins(Loop->GetValuePin(), FindPinChecked(SetTarget, TEXT("TargetTrigger")));
		Previous = SetTarget->GetThenPin();
		X += 560;
	}
	AddComment(Construction, TEXT("CONNECT SLIDER BINDINGS TO THIS ACTOR'S LOOP TRIGGER"),
		240, -80, 1180, 430, FLinearColor(0.10f, 0.42f, 0.22f));
	// SCS templates are serialized individually: a raw cross-template pointer
	// survives instancing as LoopTrigger_GEN_VARIABLE. Connect actual components
	// in Construction Script, including when an editor actor is reconstructed.
	for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
	{
		if (auto* Binding = Cast<UHapbeatParameterBinding>(Node->ComponentTemplate))
		{
			Binding->TargetTrigger = nullptr;
		}
		else if (auto* AddressPanel = Cast<UHapbeatAddressOverridePanelComponent>(Node->ComponentTemplate))
		{
			// Z4 owns this persistent HUD panel for the whole active zone. A Close
			// button would only hide its address controls while leaving the console
			// running, so the Showcase deliberately omits it.
			AddressPanel->bShowCloseButton = false;
			// The target preview is deliberately written as two full target paths.
			// Reserve their width instead of truncating the player/group suffixes.
			AddressPanel->ViewportSize = FVector2D(440.0f, 108.0f);
		}
	}
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
}

/**
 * Upgrade the one generated Space-toggle branch without rebuilding the user's
 * existing BP_Z4_StreamConsole asset. Deferred playbacks are live logical
 * streams (only their endpoint is unresolved), so they must follow Stop rather
 * than Fire. Keeping this as a narrow migration preserves any user additions
 * outside the generated toggle.
 */
void UpgradeStreamConsoleToggle(UBlueprint* Blueprint)
{
	UK2Node_CallFunction* IsActiveNode = nullptr;
	UK2Node_IfThenElse* ToggleBranch = nullptr;
	for (UEdGraph* Graph : Blueprint->UbergraphPages)
	{
		if (Graph == nullptr)
		{
			continue;
		}
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node);
				Call != nullptr
				&& Call->FunctionReference.GetMemberName() == GET_FUNCTION_NAME_CHECKED(UHapbeatStreamPlayback, IsActive))
			{
				IsActiveNode = Call;
			}
		}
	}

	if (IsActiveNode == nullptr)
	{
		return; // Already migrated (or an asset unrelated to the generated Z4 toggle).
	}
	for (UEdGraph* Graph : Blueprint->UbergraphPages)
	{
		if (Graph == nullptr)
		{
			continue;
		}
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (UK2Node_IfThenElse* Branch = Cast<UK2Node_IfThenElse>(Node);
				Branch != nullptr && Branch->GetConditionPin()->LinkedTo.Contains(IsActiveNode->GetReturnValuePin()))
			{
				ToggleBranch = Branch;
				break;
			}
		}
	}
	if (ToggleBranch == nullptr)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Hapbeat] Could not find the Z4 Space-toggle branch to migrate."));
		return;
	}

	const TArray<UEdGraphPin*> OldThenLinks = ToggleBranch->GetThenPin()->LinkedTo;
	const TArray<UEdGraphPin*> OldElseLinks = ToggleBranch->GetElsePin()->LinkedTo;
	ToggleBranch->GetThenPin()->BreakAllPinLinks();
	ToggleBranch->GetElsePin()->BreakAllPinLinks();
	for (UEdGraphPin* Pin : OldThenLinks)
	{
		ConnectPins(ToggleBranch->GetElsePin(), Pin);
	}
	for (UEdGraphPin* Pin : OldElseLinks)
	{
		ConnectPins(ToggleBranch->GetThenPin(), Pin);
	}
	IsActiveNode->FunctionReference.SetExternalMember(
		GET_FUNCTION_NAME_CHECKED(UHapbeatStreamPlayback, IsStopped), UHapbeatStreamPlayback::StaticClass());
	IsActiveNode->ReconstructNode();
	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
}

void CreateStreamConsoleBlueprint(UBlueprint* Blueprint, UWidgetBlueprint* WidgetBlueprint, UHapbeatEventMap* EventMap)
{
	check(WidgetBlueprint != nullptr);
	ClearGeneratedGraph(Blueprint);
	USCS_Node* Root = AddSceneRoot(Blueprint);
	const FGuid LoopId = FindEntryId(EventMap, TEXT("z4_stream_loop"));
	const FGuid TickId = FindEntryId(EventMap, TEXT("z4_slider_tick"));

	USCS_Node* LoopNode = AddComponent(Blueprint, Root, UHapbeatTriggerComponent::StaticClass(), TEXT("LoopTrigger"));
	UHapbeatTriggerComponent* Loop = CastChecked<UHapbeatTriggerComponent>(LoopNode->ComponentTemplate);
	Loop->EventMap = EventMap;
	Loop->EntryId = LoopId;

	USCS_Node* TickNode = AddComponent(Blueprint, Root, UHapbeatTriggerComponent::StaticClass(), TEXT("TickTrigger"));
	UHapbeatTriggerComponent* Tick = CastChecked<UHapbeatTriggerComponent>(TickNode->ComponentTemplate);
	Tick->EventMap = EventMap;
	Tick->EntryId = TickId;

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
		// Assigned to runtime components by the Construction Script below.
	};
	AddBinding(TEXT("GainBinding"), EHapbeatBindingOutput::StreamGain, 0.0f, 1.0f, 0.0f, 1.0f);
	AddBinding(TEXT("PanBinding"), EHapbeatBindingOutput::StreamPan, -1.0f, 1.0f, -1.0f, 1.0f);

	USCS_Node* AddressNode = AddComponent(Blueprint, Root, UHapbeatAddressOverridePanelComponent::StaticClass(), TEXT("AddressPanel"));
	UHapbeatAddressOverridePanelComponent* AddressPanel = CastChecked<UHapbeatAddressOverridePanelComponent>(AddressNode->ComponentTemplate);
	AddressPanel->bShowOnBeginPlay = false;
	AddressPanel->bPersistOnApply = true;
	AddressPanel->bShowCloseButton = false;
	AddressPanel->ViewportHAlign = HAlign_Center;
	AddressPanel->ViewportVAlign = VAlign_Top;
	AddressPanel->ViewportPadding = FMargin(8.0f);
	AddressPanel->ViewportSize = FVector2D(440.0f, 108.0f);

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	AddObjectMember(Blueprint, TEXT("ConsoleWidget"), UHapbeatShowcaseZ4ConsoleWidget::StaticClass());
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	UEdGraph* Graph = GetEventGraph(Blueprint);
	USoundBase* TickSound = LoadObject<USoundBase>(nullptr,
		TEXT("/HapbeatSDK/HapbeatSamples/Showcase/Sounds/S_z4_ui_tick.S_z4_ui_tick"));
	ConfigureStreamBindingTargets(Blueprint);

	AddComment(Graph, TEXT("SPACE  |  live or Deferred loop: Stop  |  otherwise: Fire"), -1540, -490, 2260, 240,
		FLinearColor(0.18f, 0.18f, 0.18f));
	AddComment(Graph, TEXT("ON SHOWCASE ZONE ACTIVATED  |  seed bindings, create console, show address override"), -1540, -170, 2440, 230,
		FLinearColor(0.10f, 0.42f, 0.22f));
	AddComment(Graph, TEXT("ON SHOWCASE ZONE DEACTIVATED  |  stop loop, remove console, hide address override"), -1540, 150, 2180, 220,
		FLinearColor(0.50f, 0.15f, 0.15f));

	UK2Node_InputKey* ToggleInput = AddKeyEvent(Graph, EKeys::SpaceBar, -1450, -410);
	UK2Node_VariableGet* LoopForPlayback = AddComponentGet(Graph, TEXT("LoopTrigger"), -1220, -310);
	UK2Node_CallFunction* GetPlayback = AddCall(Graph, UHapbeatTriggerComponent::StaticClass(),
		GET_FUNCTION_NAME_CHECKED(UHapbeatTriggerComponent, GetActivePlayback), -970, -410);
	UK2Node_CallFunction* HasPlayback = AddCall(Graph, UKismetSystemLibrary::StaticClass(),
		GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, IsValid), -700, -410);
	UK2Node_IfThenElse* PlaybackExists = AddNode<UK2Node_IfThenElse>(Graph, -440, -410);
	UK2Node_CallFunction* IsStopped = AddCall(Graph, UHapbeatStreamPlayback::StaticClass(),
		GET_FUNCTION_NAME_CHECKED(UHapbeatStreamPlayback, IsStopped), -180, -410);
	UK2Node_IfThenElse* IsStoppedBranch = AddNode<UK2Node_IfThenElse>(Graph, 80, -410);
	UK2Node_VariableGet* LoopForStop = AddComponentGet(Graph, TEXT("LoopTrigger"), 320, -290);
	UK2Node_CallFunction* StopLoop = AddCall(Graph, UHapbeatTriggerComponent::StaticClass(),
		GET_FUNCTION_NAME_CHECKED(UHapbeatTriggerComponent, Stop), 550, -410);
	UK2Node_VariableGet* LoopForFire = AddComponentGet(Graph, TEXT("LoopTrigger"), 320, -70);
	UK2Node_CallFunction* FireLoop = AddCall(Graph, UHapbeatTriggerComponent::StaticClass(),
		GET_FUNCTION_NAME_CHECKED(UHapbeatTriggerComponent, Fire), 550, -190);
	ConnectPins(FindPinChecked(ToggleInput, TEXT("Pressed")), PlaybackExists->GetExecPin());
	ConnectPins(LoopForPlayback->GetValuePin(), FindTargetPinChecked(GetPlayback));
	ConnectPins(GetPlayback->GetReturnValuePin(), FindPinChecked(HasPlayback, TEXT("Object")));
	ConnectPins(HasPlayback->GetReturnValuePin(), PlaybackExists->GetConditionPin());
	ConnectPins(PlaybackExists->GetThenPin(), IsStoppedBranch->GetExecPin());
	ConnectPins(PlaybackExists->GetElsePin(), FireLoop->GetExecPin());
	ConnectPins(GetPlayback->GetReturnValuePin(), FindTargetPinChecked(IsStopped));
	ConnectPins(IsStopped->GetReturnValuePin(), IsStoppedBranch->GetConditionPin());
	ConnectPins(IsStoppedBranch->GetThenPin(), FireLoop->GetExecPin());
	ConnectPins(LoopForStop->GetValuePin(), FindTargetPinChecked(StopLoop));
	ConnectPins(IsStoppedBranch->GetElsePin(), StopLoop->GetExecPin());
	ConnectPins(LoopForFire->GetValuePin(), FindTargetPinChecked(FireLoop));

	UK2Node_Event* Activated = AddOverrideEvent(Graph, AHapbeatShowcaseBlueprintZoneActor::StaticClass(),
		GET_FUNCTION_NAME_CHECKED(AHapbeatShowcaseBlueprintZoneActor, ReceiveZoneActivated), -1450, -90);
	UK2Node_VariableGet* GainForSeed = AddComponentGet(Graph, TEXT("GainBinding"), -1210, 20);
	UK2Node_CallFunction* SeedGain = AddCall(Graph, UHapbeatParameterBinding::StaticClass(),
		GET_FUNCTION_NAME_CHECKED(UHapbeatParameterBinding, SetValue), -970, -90);
	FindPinChecked(SeedGain, TEXT("Value"))->DefaultValue = TEXT("0.5");
	UK2Node_VariableGet* PanForSeed = AddComponentGet(Graph, TEXT("PanBinding"), -730, 20);
	UK2Node_CallFunction* SeedPan = AddCall(Graph, UHapbeatParameterBinding::StaticClass(),
		GET_FUNCTION_NAME_CHECKED(UHapbeatParameterBinding, SetValue), -500, -90);
	FindPinChecked(SeedPan, TEXT("Value"))->DefaultValue = TEXT("0.0");
	UK2Node_CallFunction* CreateWidget = AddCall(Graph, UWidgetBlueprintLibrary::StaticClass(),
		GET_FUNCTION_NAME_CHECKED(UWidgetBlueprintLibrary, Create), -250, -90);
	FindPinChecked(CreateWidget, TEXT("WidgetType"))->DefaultObject = WidgetBlueprint->GeneratedClass;
	UK2Node_DynamicCast* ConsoleCast = NewObject<UK2Node_DynamicCast>(Graph);
	ConsoleCast->TargetType = UHapbeatShowcaseZ4ConsoleWidget::StaticClass();
	Graph->AddNode(ConsoleCast, false, false);
	ConsoleCast->NodePosX = 10;
	ConsoleCast->NodePosY = -90;
	ConsoleCast->CreateNewGuid();
	ConsoleCast->PostPlacedNewNode();
	ConsoleCast->AllocateDefaultPins();
	UK2Node_VariableGet* GainForWidget = AddComponentGet(Graph, TEXT("GainBinding"), 220, 80);
	UK2Node_VariableGet* PanForWidget = AddComponentGet(Graph, TEXT("PanBinding"), 220, 180);
	UK2Node_VariableGet* TickForWidget = AddComponentGet(Graph, TEXT("TickTrigger"), 220, 280);
	UK2Node_CallFunction* ConfigureWidget = AddCall(Graph, UHapbeatShowcaseZ4ConsoleWidget::StaticClass(),
		GET_FUNCTION_NAME_CHECKED(UHapbeatShowcaseZ4ConsoleWidget, Configure), 520, -90);
	FindPinChecked(ConfigureWidget, TEXT("InTickSound"))->DefaultObject = TickSound;
	UK2Node_CallFunction* AddToViewport = AddCall(Graph, UUserWidget::StaticClass(),
		GET_FUNCTION_NAME_CHECKED(UUserWidget, AddToViewport), 760, -90);
	UK2Node_VariableSet* StoreWidget = AddSelfVariableSet(Graph, TEXT("ConsoleWidget"), 1000, -90);
	UK2Node_VariableGet* AddressForShow = AddComponentGet(Graph, TEXT("AddressPanel"), 1220, 80);
	UK2Node_CallFunction* ShowAddress = AddCall(Graph, UHapbeatAddressOverridePanelComponent::StaticClass(),
		GET_FUNCTION_NAME_CHECKED(UHapbeatAddressOverridePanelComponent, Show), 1460, -90);
	ConnectPins(Activated->GetThenPin(), SeedGain->GetExecPin());
	ConnectPins(GainForSeed->GetValuePin(), FindTargetPinChecked(SeedGain));
	ConnectPins(SeedGain->GetThenPin(), SeedPan->GetExecPin());
	ConnectPins(PanForSeed->GetValuePin(), FindTargetPinChecked(SeedPan));
	ConnectPins(SeedPan->GetThenPin(), CreateWidget->GetExecPin());
	ConnectPins(CreateWidget->GetReturnValuePin(), ConsoleCast->GetCastSourcePin());
	ConnectPins(CreateWidget->GetThenPin(), ConsoleCast->GetExecPin());
	ConnectPins(ConsoleCast->GetValidCastPin(), ConfigureWidget->GetExecPin());
	ConnectPins(ConsoleCast->GetCastResultPin(), FindTargetPinChecked(ConfigureWidget));
	ConnectPins(GainForWidget->GetValuePin(), FindPinChecked(ConfigureWidget, TEXT("InGainBinding")));
	ConnectPins(PanForWidget->GetValuePin(), FindPinChecked(ConfigureWidget, TEXT("InPanBinding")));
	ConnectPins(TickForWidget->GetValuePin(), FindPinChecked(ConfigureWidget, TEXT("InTickTrigger")));
	ConnectPins(ConfigureWidget->GetThenPin(), AddToViewport->GetExecPin());
	ConnectPins(ConsoleCast->GetCastResultPin(), FindTargetPinChecked(AddToViewport));
	ConnectPins(AddToViewport->GetThenPin(), StoreWidget->GetExecPin());
	ConnectPins(ConsoleCast->GetCastResultPin(), FindPinChecked(StoreWidget, TEXT("ConsoleWidget")));
	ConnectPins(StoreWidget->GetThenPin(), ShowAddress->GetExecPin());
	ConnectPins(AddressForShow->GetValuePin(), FindTargetPinChecked(ShowAddress));

	UK2Node_Event* Deactivated = AddOverrideEvent(Graph, AHapbeatShowcaseBlueprintZoneActor::StaticClass(),
		GET_FUNCTION_NAME_CHECKED(AHapbeatShowcaseBlueprintZoneActor, ReceiveZoneDeactivated), -1450, 230);
	UK2Node_VariableGet* LoopForDeactivate = AddComponentGet(Graph, TEXT("LoopTrigger"), -1200, 330);
	UK2Node_CallFunction* StopForDeactivate = AddCall(Graph, UHapbeatTriggerComponent::StaticClass(),
		GET_FUNCTION_NAME_CHECKED(UHapbeatTriggerComponent, Stop), -960, 230);
	UK2Node_VariableGet* StoredWidget = AddSelfVariableGet(Graph, TEXT("ConsoleWidget"), -720, 330);
	UK2Node_CallFunction* IsWidgetValid = AddCall(Graph, UKismetSystemLibrary::StaticClass(),
		GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, IsValid), -700, 230);
	UK2Node_IfThenElse* HasConsoleWidget = AddNode<UK2Node_IfThenElse>(Graph, -480, 230);
	UK2Node_CallFunction* RemoveWidget = AddCall(Graph, UUserWidget::StaticClass(),
		GET_FUNCTION_NAME_CHECKED(UUserWidget, RemoveFromParent), -230, 180);
	UK2Node_VariableGet* AddressForHide = AddComponentGet(Graph, TEXT("AddressPanel"), 0, 330);
	UK2Node_CallFunction* HideAddress = AddCall(Graph, UHapbeatAddressOverridePanelComponent::StaticClass(),
		GET_FUNCTION_NAME_CHECKED(UHapbeatAddressOverridePanelComponent, Hide), 240, 230);
	ConnectPins(Deactivated->GetThenPin(), StopForDeactivate->GetExecPin());
	ConnectPins(LoopForDeactivate->GetValuePin(), FindTargetPinChecked(StopForDeactivate));
	ConnectPins(StopForDeactivate->GetThenPin(), HasConsoleWidget->GetExecPin());
	ConnectPins(StoredWidget->GetValuePin(), FindPinChecked(IsWidgetValid, TEXT("Object")));
	ConnectPins(IsWidgetValid->GetReturnValuePin(), HasConsoleWidget->GetConditionPin());
	ConnectPins(HasConsoleWidget->GetThenPin(), RemoveWidget->GetExecPin());
	ConnectPins(StoredWidget->GetValuePin(), FindTargetPinChecked(RemoveWidget));
	ConnectPins(RemoveWidget->GetThenPin(), HideAddress->GetExecPin());
	ConnectPins(HasConsoleWidget->GetElsePin(), HideAddress->GetExecPin());
	ConnectPins(AddressForHide->GetValuePin(), FindTargetPinChecked(HideAddress));

	SetMetadata(Blueprint, 4, TEXT("Stream Console"), FVector(-250.0f, 0.0f, 0.0f),
		{ { FText::FromString(TEXT("Space")), FText::FromString(TEXT("toggle the looping stream")) },
		  { FText::FromString(TEXT("Mouse")), FText::FromString(TEXT("drag Gain / Pan; one tick per detent")) },
		  { FText::FromString(TEXT("Top panel")), FText::FromString(TEXT("set Player / Group, then Apply")) } });
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

void CheckDoorComponentTree(const UBlueprint* Blueprint)
{
	check(Blueprint != nullptr);
	checkf(Blueprint->SimpleConstructionScript->GetAllNodes().Num() == 5,
		TEXT("BP_Z2_Door must contain exactly Root, DoorHinge, DoorFrameMesh, DoorLeafMesh, and DoorHandleMesh."));
	const UBlueprintGeneratedClass* GeneratedClass = CastChecked<UBlueprintGeneratedClass>(Blueprint->GeneratedClass);
	checkf(GeneratedClass->SimpleConstructionScript != nullptr
		&& GeneratedClass->SimpleConstructionScript->GetAllNodes().Num() == 5,
		TEXT("BP_Z2_Door's generated class must contain exactly five SCS nodes."));
}

void CheckStreamConsoleComponentTree(const UBlueprint* Blueprint)
{
	check(Blueprint != nullptr);
	checkf(Blueprint->SimpleConstructionScript->GetAllNodes().Num() == 6,
		TEXT("BP_Z4_StreamConsole must contain exactly Root, LoopTrigger, TickTrigger, GainBinding, PanBinding, and AddressPanel."));
	const UBlueprintGeneratedClass* GeneratedClass = CastChecked<UBlueprintGeneratedClass>(Blueprint->GeneratedClass);
	checkf(GeneratedClass->SimpleConstructionScript != nullptr
		&& GeneratedClass->SimpleConstructionScript->GetAllNodes().Num() == 6,
		TEXT("BP_Z4_StreamConsole's generated class must contain exactly six SCS nodes."));
}

}

void Generate()
{
	UHapbeatEventMap* EventMap = GetShowcaseEventMap();
	checkf(EventMap != nullptr, TEXT("Could not load EM_Showcase."));
	bool bDoorWasCreated = false;
	UBlueprint* Door = LoadOrCreateBlueprint(TEXT("BP_Z2_Door"), bDoorWasCreated);
	check(Door != nullptr);
	if (bDoorWasCreated)
	{
		CreateDoorBlueprint(Door, EventMap);
		FKismetEditorUtilities::CompileBlueprint(Door);
		FAssetRegistryModule::AssetCreated(Door);
		Door->MarkPackageDirty();
		UPackage::SavePackage(Door->GetOutermost(), Door, *FPackageName::LongPackageNameToFilename(Door->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension()), FSavePackageArgs());
	}
	CheckDoorComponentTree(Door);
	bool bWidgetWasCreated = false;
	UWidgetBlueprint* StreamWidget = LoadOrCreateWidgetBlueprint(TEXT("BP_Z4_StreamConsoleWidget"), bWidgetWasCreated);
	check(StreamWidget != nullptr);
	if (bWidgetWasCreated)
	{
		CreateStreamConsoleWidgetBlueprint(StreamWidget);
		SaveBlueprintAsset(StreamWidget);
	}
	bool bStreamWasCreated = false;
	UBlueprint* Stream = LoadOrCreateBlueprint(TEXT("BP_Z4_StreamConsole"), bStreamWasCreated);
	check(Stream != nullptr);
	if (bStreamWasCreated)
	{
		CreateStreamConsoleBlueprint(Stream, StreamWidget, EventMap);
		SaveBlueprintAsset(Stream);
	}
	CheckStreamConsoleComponentTree(Stream);

	UWorld* World = GEditor->GetEditorWorldContext().World();
	checkf(World != nullptr && World->GetOutermost()->GetName() == MapPath, TEXT("Open the Showcase map before running Hapbeat.GenerateBlueprintShowcase."));
	ReplaceZoneActor(World, TEXT("Z2_Door"), Door->GeneratedClass, FVector(0.0f, 3000.0f, 0.0f));
	ReplaceZoneActor(World, TEXT("Z4_StreamConsole"), Stream->GeneratedClass, FVector(0.0f, 9000.0f, 0.0f));
	FEditorFileUtils::SaveLevel(World->PersistentLevel);
	UE_LOG(LogTemp, Display, TEXT("[Hapbeat] Generated BP_Z2_Door and BP_Z4_StreamConsole in the Showcase map."));
}

void GenerateStreamConsoleAssets()
{
	UHapbeatEventMap* EventMap = GetShowcaseEventMap();
	checkf(EventMap != nullptr, TEXT("Could not load EM_Showcase."));
	bool bWidgetWasCreated = false;
	UWidgetBlueprint* StreamWidget = LoadOrCreateWidgetBlueprint(TEXT("BP_Z4_StreamConsoleWidget"), bWidgetWasCreated);
	check(StreamWidget != nullptr);
	if (bWidgetWasCreated)
	{
		CreateStreamConsoleWidgetBlueprint(StreamWidget);
		SaveBlueprintAsset(StreamWidget);
	}
	bool bStreamWasCreated = false;
	UBlueprint* Stream = LoadOrCreateBlueprint(TEXT("BP_Z4_StreamConsole"), bStreamWasCreated);
	check(Stream != nullptr);
	if (bStreamWasCreated)
	{
		CreateStreamConsoleBlueprint(Stream, StreamWidget, EventMap);
		SaveBlueprintAsset(Stream);
	}
	UpgradeStreamConsoleToggle(Stream);
	ConfigureStreamBindingTargets(Stream);
	SaveBlueprintAsset(Stream);
	CheckStreamConsoleComponentTree(Stream);
	UE_LOG(LogTemp, Display, TEXT("[Hapbeat] Generated Z4 Stream Console Blueprint assets without changing the Showcase map."));
}

void GenerateDoorAsset()
{
	UHapbeatEventMap* EventMap = GetShowcaseEventMap();
	checkf(EventMap != nullptr, TEXT("Could not load EM_Showcase."));
	bool bDoorWasCreated = false;
	UBlueprint* Door = LoadOrCreateBlueprint(TEXT("BP_Z2_Door"), bDoorWasCreated);
	check(Door != nullptr);
	if (bDoorWasCreated)
	{
		CreateDoorBlueprint(Door, EventMap);
		FKismetEditorUtilities::CompileBlueprint(Door);
		FAssetRegistryModule::AssetCreated(Door);
		Door->MarkPackageDirty();
		UPackage::SavePackage(Door->GetOutermost(), Door,
			*FPackageName::LongPackageNameToFilename(Door->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension()),
			FSavePackageArgs());
	}
	CheckDoorComponentTree(Door);
	UE_LOG(LogTemp, Display, TEXT("[Hapbeat] Generated BP_Z2_Door without changing the Showcase map."));
}

}
