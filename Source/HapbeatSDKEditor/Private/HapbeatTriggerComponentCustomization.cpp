// Copyright (c) 2026 Hapbeat. MIT License.
#include "HapbeatTriggerComponentCustomization.h"

#include "HapbeatEntryPicker.h"
#include "HapbeatEventMap.h"
#include "HapbeatSequenceComponent.h"
#include "HapbeatTriggerComponent.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IPropertyUtilities.h" // complete type for RequestForceRefresh (do not rely on unity-build include order)
#include "PropertyHandle.h"
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
	HapbeatEditor::BuildEntryOptions(Map, State->Options);
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

TSharedRef<SWidget> FHapbeatTriggerComponentCustomization::OnGenerateOptionWidget(TSharedPtr<FGuid> InId, TWeakPtr<FEntryPickerState> WeakState) const
{
	FText Label = HapbeatEditor::DescribeEntryById(FGuid(), nullptr); // "(none)"
	if (InId.IsValid() && InId->IsValid())
	{
		if (TSharedPtr<FEntryPickerState> State = WeakState.Pin())
		{
			Label = HapbeatEditor::DescribeEntryById(*InId, State->WeakMap.Get());
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
	HapbeatEditor::WriteGuidToHandle(State->GuidHandle, NewGuid);
}

FText FHapbeatTriggerComponentCustomization::GetSelectedLabel(TWeakPtr<FEntryPickerState> WeakState) const
{
	TSharedPtr<FEntryPickerState> State = WeakState.Pin();
	if (!State.IsValid())
	{
		return FText::GetEmpty();
	}

	FGuid CurrentId;
	if (!HapbeatEditor::ReadGuidFromHandle(State->GuidHandle, CurrentId))
	{
		return LOCTEXT("MultipleValues", "Multiple Values");
	}
	return HapbeatEditor::DescribeEntryById(CurrentId, State->WeakMap.Get());
}

#undef LOCTEXT_NAMESPACE
