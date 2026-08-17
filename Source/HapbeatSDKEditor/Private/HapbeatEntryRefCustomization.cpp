// Copyright (c) 2026 Hapbeat. MIT License.
#include "HapbeatEntryRefCustomization.h"

#include "HapbeatEntryPicker.h"
#include "HapbeatEntryRef.h"
#include "HapbeatEventMap.h"

#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IPropertyUtilities.h" // complete type for ForceRefresh (do not rely on unity-build include order)
#include "PropertyHandle.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "FHapbeatEntryRefCustomization"

namespace
{
	/** Meta specifier naming the sibling UHapbeatEventMap property. See the class doc. */
	const TCHAR* EventMapMetaKey = TEXT("HapbeatEventMap");
}

TSharedRef<IPropertyTypeCustomization> FHapbeatEntryRefCustomization::MakeInstance()
{
	return MakeShared<FHapbeatEntryRefCustomization>();
}

TSharedPtr<IPropertyHandle> FHapbeatEntryRefCustomization::FindEventMapHandle(
	const TSharedRef<IPropertyHandle>& StructPropertyHandle) const
{
	const FString SiblingName = StructPropertyHandle->GetMetaData(EventMapMetaKey);
	if (SiblingName.IsEmpty())
	{
		return nullptr;
	}
	// The parent handle is the owning object / struct, so its children are this
	// property's siblings -- the same route the engine's own customizations take
	// to reach a related property.
	const TSharedPtr<IPropertyHandle> ParentHandle = StructPropertyHandle->GetParentHandle();
	if (!ParentHandle.IsValid())
	{
		return nullptr;
	}
	return ParentHandle->GetChildHandle(FName(*SiblingName));
}

void FHapbeatEntryRefCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle,
	FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	PropertyUtilities = CustomizationUtils.GetPropertyUtilities();

	EntryIdHandle = StructPropertyHandle->GetChildHandle(
		GET_MEMBER_NAME_CHECKED(FHapbeatEntryRef, EntryId));
	EventMapHandle = FindEventMapHandle(StructPropertyHandle);

	HeaderRow.NameContent()
	[
		StructPropertyHandle->CreatePropertyNameWidget()
	];

	if (!EntryIdHandle.IsValid() || ResolveEventMap() == nullptr)
	{
		// No map to list entries from. Show the property's own default widget
		// (the four GUID components) rather than an empty dropdown, so the value
		// stays editable, and say in the tooltip how to get the by-name picker.
		HeaderRow.ValueContent()
		.MinDesiredWidth(250.0f)
		[
			SNew(SBox)
			.ToolTipText(LOCTEXT("NoEventMapTooltip",
				"No Event Map to list entries from. Add meta = (HapbeatEventMap=\"<property name>\") to this property, pointing at the UHapbeatEventMap property on the same class, and assign that map -- then this row becomes a dropdown of entry names."))
			[
				EntryIdHandle.IsValid()
					? EntryIdHandle->CreatePropertyValueWidget()
					: StructPropertyHandle->CreatePropertyValueWidget()
			]
		];
		return;
	}

	// Live: assigning / clearing the sibling map rebuilds this row, so it flips
	// between the raw GUID widget and the picker immediately.
	if (EventMapHandle.IsValid())
	{
		EventMapHandle->SetOnPropertyValueChanged(
			FSimpleDelegate::CreateSP(this, &FHapbeatEntryRefCustomization::HandleEventMapChanged));
	}

	RefreshEntryOptions();

	HeaderRow.ValueContent()
	.MinDesiredWidth(250.0f)
	.MaxDesiredWidth(400.0f)
	[
		SAssignNew(EntryCombo, SComboBox<TSharedPtr<FGuid>>)
		.OptionsSource(&EntryOptions)
		// Rebuild on open so entries added to the map after this panel was drawn
		// are offered without reselecting the object.
		.OnComboBoxOpening(this, &FHapbeatEntryRefCustomization::RefreshEntryOptions)
		.OnGenerateWidget(this, &FHapbeatEntryRefCustomization::OnGenerateEntryWidget)
		.OnSelectionChanged(this, &FHapbeatEntryRefCustomization::OnEntrySelected)
		[
			SNew(STextBlock)
			.Text(this, &FHapbeatEntryRefCustomization::GetSelectedEntryLabel)
			.Font(IDetailLayoutBuilder::GetDetailFont())
		]
	];
}

UHapbeatEventMap* FHapbeatEntryRefCustomization::ResolveEventMap() const
{
	if (!EventMapHandle.IsValid())
	{
		return nullptr;
	}
	UObject* Value = nullptr;
	// Multi-select with differing maps returns MultipleValues, which is exactly
	// the case where a single shared entry list would be meaningless.
	if (EventMapHandle->GetValue(Value) != FPropertyAccess::Success)
	{
		return nullptr;
	}
	return Cast<UHapbeatEventMap>(Value);
}

void FHapbeatEntryRefCustomization::RefreshEntryOptions()
{
	HapbeatEditor::BuildEntryOptions(ResolveEventMap(), EntryOptions);
	if (EntryCombo.IsValid())
	{
		EntryCombo->RefreshOptions();
	}
}

void FHapbeatEntryRefCustomization::HandleEventMapChanged()
{
	// An entry id only means something inside the map that owns it, so keeping
	// the previous selection would leave an unresolvable reference behind.
	HapbeatEditor::WriteGuidToHandle(EntryIdHandle, FGuid());
	if (PropertyUtilities.IsValid())
	{
		// Rebuild the row so the dropdown lists the NEW map's entries (and so the
		// raw-row fallback appears / disappears with the map).
		PropertyUtilities->ForceRefresh();
	}
}

TSharedRef<SWidget> FHapbeatEntryRefCustomization::OnGenerateEntryWidget(TSharedPtr<FGuid> InId) const
{
	const FGuid Id = InId.IsValid() ? *InId : FGuid();
	return SNew(STextBlock)
		.Text(HapbeatEditor::DescribeEntryById(Id, ResolveEventMap()))
		.Font(IDetailLayoutBuilder::GetDetailFont());
}

void FHapbeatEntryRefCustomization::OnEntrySelected(TSharedPtr<FGuid> NewSelection, ESelectInfo::Type /*SelectInfo*/)
{
	HapbeatEditor::WriteGuidToHandle(EntryIdHandle, NewSelection.IsValid() ? *NewSelection : FGuid());
}

FText FHapbeatEntryRefCustomization::GetSelectedEntryLabel() const
{
	FGuid CurrentId;
	if (!HapbeatEditor::ReadGuidFromHandle(EntryIdHandle, CurrentId))
	{
		return LOCTEXT("MultipleValues", "Multiple Values");
	}
	return HapbeatEditor::DescribeEntryById(CurrentId, ResolveEventMap());
}

#undef LOCTEXT_NAMESPACE
