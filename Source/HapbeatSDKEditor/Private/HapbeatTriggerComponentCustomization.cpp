// Copyright (c) 2026 Hapbeat. MIT License.
#include "HapbeatTriggerComponentCustomization.h"

#include "HapbeatEventMap.h"
#include "HapbeatSequenceComponent.h"
#include "HapbeatTriggerComponent.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IPropertyUtilities.h" // complete type for RequestForceRefresh (do not rely on unity-build include order)
#include "PropertyHandle.h"
#include "ScopedTransaction.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "FHapbeatTriggerComponentCustomization"

TSharedRef<IDetailCustomization> FHapbeatTriggerComponentCustomization::MakeInstance()
{
	return MakeShared<FHapbeatTriggerComponentCustomization>();
}

void FHapbeatTriggerComponentCustomization::CustomizeDetails(const TSharedPtr<IDetailLayoutBuilder>& DetailBuilder)
{
	WeakDetailBuilder = DetailBuilder;
	CustomizeDetails(*DetailBuilder);
}

void FHapbeatTriggerComponentCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	Pickers.Reset();

	TArray<TWeakObjectPtr<UObject>> Objects;
	DetailBuilder.GetObjectsBeingCustomized(Objects);

	// Multi-select: a shared picker across differently-configured components
	// would be ambiguous (different EventMaps / different valid entries), so
	// fall back to the default per-property FGuid rows entirely.
	if (Objects.Num() != 1)
	{
		return;
	}

	UHapbeatTriggerComponent* Component = Cast<UHapbeatTriggerComponent>(Objects[0].Get());
	if (Component == nullptr)
	{
		return;
	}

	const TSharedRef<IPropertyHandle> EventMapHandle =
		DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UHapbeatTriggerComponent, EventMap));
	// Live: re-run CustomizeDetails (via a full details-panel refresh) whenever
	// EventMap is (re)assigned or cleared, so the raw-row <-> picker swap for
	// every FGuid property below reflects the CURRENT EventMap, not the one
	// that happened to be set when the panel was first opened.
	EventMapHandle->SetOnPropertyValueChanged(
		FSimpleDelegate::CreateSP(this, &FHapbeatTriggerComponentCustomization::HandleEventMapChanged));

	IDetailCategoryBuilder& BaseCategory = DetailBuilder.EditCategory("Hapbeat");
	BuildEntryPicker(DetailBuilder, BaseCategory, EventMapHandle,
		GET_MEMBER_NAME_CHECKED(UHapbeatTriggerComponent, EntryId),
		UHapbeatTriggerComponent::StaticClass(),
		LOCTEXT("EntryId", "Entry Id"));

	if (Component->IsA<UHapbeatSequenceComponent>())
	{
		// Matches the UPROPERTY(Category = "Hapbeat|Sequence") declared on both
		// fields in HapbeatSequenceComponent.h. The owning class MUST be passed
		// explicitly: this customization is registered on the BASE class, so
		// GetProperty() with the default ClassOutermost=null resolves names
		// against the base-class property map and returns an INVALID handle for
		// subclass-owned properties (verified in DetailLayoutBuilderImpl.cpp).
		IDetailCategoryBuilder& SequenceCategory = DetailBuilder.EditCategory("Hapbeat|Sequence");
		BuildEntryPicker(DetailBuilder, SequenceCategory, EventMapHandle,
			GET_MEMBER_NAME_CHECKED(UHapbeatSequenceComponent, StartEntryId),
			UHapbeatSequenceComponent::StaticClass(),
			LOCTEXT("StartEntryId", "Start Entry Id"));
		BuildEntryPicker(DetailBuilder, SequenceCategory, EventMapHandle,
			GET_MEMBER_NAME_CHECKED(UHapbeatSequenceComponent, StopEntryId),
			UHapbeatSequenceComponent::StaticClass(),
			LOCTEXT("StopEntryId", "Stop Entry Id"));
	}
}

void FHapbeatTriggerComponentCustomization::HandleEventMapChanged()
{
	if (TSharedPtr<IDetailLayoutBuilder> Builder = WeakDetailBuilder.Pin())
	{
		Builder->GetPropertyUtilities()->RequestForceRefresh();
	}
}

void FHapbeatTriggerComponentCustomization::BuildEntryPicker(IDetailLayoutBuilder& DetailBuilder,
	IDetailCategoryBuilder& Category, TSharedRef<IPropertyHandle> EventMapHandle, FName PropertyName,
	const UClass* OwningClass, const FText& DisplayLabel)
{
	// Resolve against the class that DECLARES the property (see CustomizeDetails).
	const TSharedRef<IPropertyHandle> GuidHandle = DetailBuilder.GetProperty(PropertyName, OwningClass);

	UHapbeatEventMap* Map = ResolveEventMap(EventMapHandle);
	if (Map == nullptr)
	{
		// No EventMap assigned (yet): leave the default raw FGuid row exactly
		// as UE's built-in struct display renders it (four int32 spinners) --
		// there is nothing to pick from.
		return;
	}

	TSharedRef<FEntryPickerState> State = MakeShared<FEntryPickerState>();
	State->GuidHandle = GuidHandle;
	State->EventMapHandle = EventMapHandle;
	State->WeakMap = Map;
	State->Options.Add(MakeShared<FGuid>()); // index 0 = "(none)"
	for (const FHapbeatEventEntry& Entry : Map->Entries)
	{
		if (Entry.Id.IsValid())
		{
			State->Options.Add(MakeShared<FGuid>(Entry.Id));
		}
	}
	Pickers.Add(PropertyName, State);

	// Replace the default row with our picker.
	DetailBuilder.HideProperty(GuidHandle);

	TWeakPtr<FEntryPickerState> WeakState = State;
	Category.AddCustomRow(DisplayLabel)
		.NameContent()
		[
			SNew(STextBlock)
			.Text(DisplayLabel)
			.Font(IDetailLayoutBuilder::GetDetailFont())
		]
		.ValueContent()
		.MinDesiredWidth(250.0f)
		.MaxDesiredWidth(400.0f)
		[
			SNew(SComboBox<TSharedPtr<FGuid>>)
			.OptionsSource(&State->Options)
			.OnGenerateWidget(this, &FHapbeatTriggerComponentCustomization::OnGenerateOptionWidget, WeakState)
			.OnSelectionChanged(this, &FHapbeatTriggerComponentCustomization::OnOptionSelected, WeakState)
			[
				SNew(STextBlock)
				.Text(this, &FHapbeatTriggerComponentCustomization::GetSelectedLabel, WeakState)
				.Font(IDetailLayoutBuilder::GetDetailFont())
			]
		];
}

UHapbeatEventMap* FHapbeatTriggerComponentCustomization::ResolveEventMap(const TSharedPtr<IPropertyHandle>& EventMapHandle)
{
	if (!EventMapHandle.IsValid())
	{
		return nullptr;
	}
	UObject* Value = nullptr;
	if (EventMapHandle->GetValue(Value) != FPropertyAccess::Success)
	{
		return nullptr;
	}
	return Cast<UHapbeatEventMap>(Value);
}

bool FHapbeatTriggerComponentCustomization::ReadGuid(const TSharedPtr<IPropertyHandle>& Handle, FGuid& OutGuid)
{
	if (!Handle.IsValid())
	{
		return false;
	}
	TArray<void*> RawData;
	Handle->AccessRawData(RawData);
	if (RawData.Num() != 1 || RawData[0] == nullptr)
	{
		return false;
	}
	OutGuid = *static_cast<FGuid*>(RawData[0]);
	return true;
}

void FHapbeatTriggerComponentCustomization::WriteGuid(const TSharedPtr<IPropertyHandle>& Handle, const FGuid& NewGuid)
{
	if (!Handle.IsValid())
	{
		return;
	}
	// Per-component (A/B/C/D uint32) write via the 4 child handles -- the same
	// mechanism the engine's own FGuid struct customization uses
	// (Editor/DetailCustomizations/Private/GuidStructCustomization.cpp,
	// WriteGuidToProperty): FGuid has no direct IPropertyHandle::SetValue
	// overload, but its 4 int32 fields (A, B, C, D; see
	// CoreUObject/Public/UObject/NoExportTypes.h) are reflected as ordinary
	// child properties, indices 0..3 in that order.
	FScopedTransaction Transaction(LOCTEXT("SetEntryId", "Set Hapbeat Entry Id"));
	for (int32 ChildIndex = 0; ChildIndex < 4; ++ChildIndex)
	{
		TSharedPtr<IPropertyHandle> ChildHandle = Handle->GetChildHandle(ChildIndex);
		if (!ChildHandle.IsValid())
		{
			continue;
		}
		// First 3 components are flagged interactive + non-transactable so only
		// one combined undo transaction (the FScopedTransaction above) is
		// created for the whole 4-component write, and PostEditChange doesn't
		// reinstance anything until the final component lands.
		const EPropertyValueSetFlags::Type Flags = (ChildIndex != 3)
			? (EPropertyValueSetFlags::InteractiveChange | EPropertyValueSetFlags::NotTransactable)
			: EPropertyValueSetFlags::NotTransactable;
		ChildHandle->SetValue(static_cast<int32>(NewGuid[ChildIndex]), Flags);
	}
}

FText FHapbeatTriggerComponentCustomization::DescribeEntryById(const FGuid& Id, UHapbeatEventMap* Map)
{
	if (!Id.IsValid())
	{
		return LOCTEXT("NoneEntry", "(none)");
	}
	FHapbeatEventEntry Entry;
	if (Map != nullptr && Map->FindById(Id, Entry))
	{
		if (!Entry.DisplayName.IsEmpty())
		{
			return FText::FromString(Entry.DisplayName);
		}
		const FString EventId = Entry.GetEventId();
		if (!EventId.IsEmpty())
		{
			return FText::FromString(EventId);
		}
		return FText::Format(LOCTEXT("EntryShortGuid", "(unnamed {0})"), FText::FromString(Id.ToString().Left(8)));
	}
	return FText::Format(LOCTEXT("StaleEntry", "(stale: {0})"), FText::FromString(Id.ToString().Left(8)));
}

TSharedRef<SWidget> FHapbeatTriggerComponentCustomization::OnGenerateOptionWidget(TSharedPtr<FGuid> InId, TWeakPtr<FEntryPickerState> WeakState) const
{
	FText Label = LOCTEXT("NoneEntry", "(none)");
	if (InId.IsValid() && InId->IsValid())
	{
		if (TSharedPtr<FEntryPickerState> State = WeakState.Pin())
		{
			Label = DescribeEntryById(*InId, State->WeakMap.Get());
		}
	}
	return SNew(STextBlock)
		.Text(Label)
		.Font(IDetailLayoutBuilder::GetDetailFont());
}

void FHapbeatTriggerComponentCustomization::OnOptionSelected(TSharedPtr<FGuid> NewSelection, ESelectInfo::Type SelectInfo, TWeakPtr<FEntryPickerState> WeakState)
{
	TSharedPtr<FEntryPickerState> State = WeakState.Pin();
	if (!State.IsValid())
	{
		return;
	}
	const FGuid NewGuid = NewSelection.IsValid() ? *NewSelection : FGuid();
	WriteGuid(State->GuidHandle, NewGuid);
}

FText FHapbeatTriggerComponentCustomization::GetSelectedLabel(TWeakPtr<FEntryPickerState> WeakState) const
{
	TSharedPtr<FEntryPickerState> State = WeakState.Pin();
	if (!State.IsValid())
	{
		return FText::GetEmpty();
	}

	FGuid CurrentId;
	if (!ReadGuid(State->GuidHandle, CurrentId))
	{
		return LOCTEXT("MultipleValues", "Multiple Values");
	}
	return DescribeEntryById(CurrentId, State->WeakMap.Get());
}

#undef LOCTEXT_NAMESPACE
