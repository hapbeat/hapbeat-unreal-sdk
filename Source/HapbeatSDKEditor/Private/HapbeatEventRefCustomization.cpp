// Copyright (c) 2026 Hapbeat. MIT License.
#include "HapbeatEventRefCustomization.h"

#include "HapbeatEntryPicker.h"
#include "HapbeatEventMap.h"
#include "HapbeatEventRef.h"

#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "IPropertyUtilities.h" // complete type for ForceRefresh (do not rely on unity-build include order)
#include "PropertyHandle.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "FHapbeatEventRefCustomization"

TSharedRef<IPropertyTypeCustomization> FHapbeatEventRefCustomization::MakeInstance()
{
	return MakeShared<FHapbeatEventRefCustomization>();
}

void FHapbeatEventRefCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle,
	FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	PropertyUtilities = CustomizationUtils.GetPropertyUtilities();

	EventMapHandle = StructPropertyHandle->GetChildHandle(
		GET_MEMBER_NAME_CHECKED(FHapbeatEventRef, EventMap));
	EntryIdHandle = StructPropertyHandle->GetChildHandle(
		GET_MEMBER_NAME_CHECKED(FHapbeatEventRef, EntryId));

	// Header value = the chosen entry's name, so a collapsed row already says
	// which event this is; the two editable rows live in CustomizeChildren.
	HeaderRow
		.NameContent()
		[
			StructPropertyHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		.MinDesiredWidth(250.0f)
		[
			SNew(STextBlock)
			.Text(this, &FHapbeatEventRefCustomization::GetSelectedEntryLabel)
			.Font(IDetailLayoutBuilder::GetDetailFont())
		];
}

void FHapbeatEventRefCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> /*StructPropertyHandle*/,
	IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& /*CustomizationUtils*/)
{
	if (!EventMapHandle.IsValid() || !EntryIdHandle.IsValid())
	{
		return;
	}

	// Row 1: the asset picker. The default widget for a TSoftObjectPtr property
	// already is one, so there is nothing to hand-build here.
	EventMapHandle->SetOnPropertyValueChanged(
		FSimpleDelegate::CreateSP(this, &FHapbeatEventRefCustomization::HandleEventMapChanged));
	ChildBuilder.AddProperty(EventMapHandle.ToSharedRef());

	// Row 2: entries of that map, by name, replacing the raw FGuid spinners.
	HapbeatEditor::BuildEntryOptions(ResolveEventMap(), EntryOptions);

	ChildBuilder.AddCustomRow(LOCTEXT("EntryRowFilter", "Entry"))
		.NameContent()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("EntryRowLabel", "Entry"))
			.Font(IDetailLayoutBuilder::GetDetailFont())
		]
		.ValueContent()
		.MinDesiredWidth(250.0f)
		.MaxDesiredWidth(400.0f)
		[
			SNew(SComboBox<TSharedPtr<FGuid>>)
			.OptionsSource(&EntryOptions)
			.OnGenerateWidget(this, &FHapbeatEventRefCustomization::OnGenerateEntryWidget)
			.OnSelectionChanged(this, &FHapbeatEventRefCustomization::OnEntrySelected)
			[
				SNew(STextBlock)
				.Text(this, &FHapbeatEventRefCustomization::GetSelectedEntryLabel)
				.Font(IDetailLayoutBuilder::GetDetailFont())
			]
		];
}

UHapbeatEventMap* FHapbeatEventRefCustomization::ResolveEventMap() const
{
	if (!EventMapHandle.IsValid())
	{
		return nullptr;
	}
	TArray<void*> RawData;
	EventMapHandle->AccessRawData(RawData);
	if (RawData.Num() != 1 || RawData[0] == nullptr)
	{
		// Multi-select (or no value): the entry list would be ambiguous, so the
		// dropdown falls back to the "(none)"-only list and reports it.
		return nullptr;
	}
	// Editor-only, user-driven: a synchronous load is acceptable and the entry
	// names are only inside the asset anyway.
	return static_cast<TSoftObjectPtr<UHapbeatEventMap>*>(RawData[0])->LoadSynchronous();
}

void FHapbeatEventRefCustomization::HandleEventMapChanged()
{
	// An entry id only means something inside the map that owns it, so keeping
	// the previous selection would leave an unresolvable reference behind.
	HapbeatEditor::WriteGuidToHandle(EntryIdHandle, FGuid());
	if (PropertyUtilities.IsValid())
	{
		// Rebuild the rows so the dropdown lists the NEW map's entries.
		PropertyUtilities->ForceRefresh();
	}
}

TSharedRef<SWidget> FHapbeatEventRefCustomization::OnGenerateEntryWidget(TSharedPtr<FGuid> InId) const
{
	const FGuid Id = InId.IsValid() ? *InId : FGuid();
	return SNew(STextBlock)
		.Text(HapbeatEditor::DescribeEntryById(Id, ResolveEventMap()))
		.Font(IDetailLayoutBuilder::GetDetailFont());
}

void FHapbeatEventRefCustomization::OnEntrySelected(TSharedPtr<FGuid> NewSelection, ESelectInfo::Type /*SelectInfo*/)
{
	HapbeatEditor::WriteGuidToHandle(EntryIdHandle, NewSelection.IsValid() ? *NewSelection : FGuid());
}

FText FHapbeatEventRefCustomization::GetSelectedEntryLabel() const
{
	FGuid CurrentId;
	if (!HapbeatEditor::ReadGuidFromHandle(EntryIdHandle, CurrentId))
	{
		return LOCTEXT("MultipleValues", "Multiple Values");
	}
	return HapbeatEditor::DescribeEntryById(CurrentId, ResolveEventMap());
}

#undef LOCTEXT_NAMESPACE
