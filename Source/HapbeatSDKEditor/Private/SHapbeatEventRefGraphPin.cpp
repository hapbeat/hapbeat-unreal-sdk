// Copyright (c) 2026 Hapbeat. MIT License.
#include "SHapbeatEventRefGraphPin.h"

#include "HapbeatEntryPicker.h"
#include "HapbeatEventMap.h"

#include "AssetRegistry/AssetData.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraph/EdGraphSchema.h"
#include "PropertyCustomizationHelpers.h" // SObjectPropertyEntryBox
#include "ScopedTransaction.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SHapbeatEventRefGraphPin"

void SHapbeatEventRefGraphPin::Construct(const FArguments& /*InArgs*/, UEdGraphPin* InGraphPinObj)
{
	SGraphPin::Construct(SGraphPin::FArguments(), InGraphPinObj);
}

FHapbeatEventRef SHapbeatEventRefGraphPin::ReadValue() const
{
	FHapbeatEventRef Value;
	if (GraphPinObj == nullptr)
	{
		return Value;
	}
	const FString DefaultString = GraphPinObj->GetDefaultAsString();
	if (DefaultString.IsEmpty())
	{
		// A pin that has never been touched carries no default at all; that is
		// the "nothing picked yet" state, not a parse failure.
		return Value;
	}
	UScriptStruct* Struct = FHapbeatEventRef::StaticStruct();
	Struct->ImportText(*DefaultString, &Value, /*OwnerObject*/ nullptr,
		PPF_SerializedAsImportText, /*ErrorText*/ GLog, Struct->GetName());
	return Value;
}

void SHapbeatEventRefGraphPin::WriteValue(const FHapbeatEventRef& NewValue)
{
	if (GraphPinObj == nullptr)
	{
		return;
	}
	UScriptStruct* Struct = FHapbeatEventRef::StaticStruct();
	FString Exported;
	// Defaults = nullptr on purpose: passing the value itself would delta-encode
	// it away to an empty string. We want every member written out so the pin
	// default round-trips through ImportText above.
	Struct->ExportText(Exported, &NewValue, /*Defaults*/ nullptr, /*OwnerObject*/ nullptr,
		PPF_SerializedAsImportText, /*ExportRootScope*/ nullptr);

	if (Exported == GraphPinObj->GetDefaultAsString())
	{
		// Writing an unchanged value would still open an undo transaction and
		// dirty the asset, so a mere reopen of the combo would look like an edit.
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("SetEventRef", "Set Hapbeat Event"));
	GraphPinObj->Modify();
	GraphPinObj->GetSchema()->TrySetDefaultValue(*GraphPinObj, Exported);
}

UHapbeatEventMap* SHapbeatEventRefGraphPin::ResolveEventMap() const
{
	// Editor-only path driven by user interaction, so a synchronous load is fine
	// (and unavoidable: the entry names live inside the asset).
	return ReadValue().EventMap.LoadSynchronous();
}

void SHapbeatEventRefGraphPin::RefreshEntryOptions()
{
	HapbeatEditor::BuildEntryOptions(ResolveEventMap(), EntryOptions);
	if (EntryCombo.IsValid())
	{
		EntryCombo->RefreshOptions();
	}
}

FString SHapbeatEventRefGraphPin::GetEventMapPath() const
{
	return ReadValue().EventMap.ToString();
}

void SHapbeatEventRefGraphPin::OnEventMapChanged(const FAssetData& AssetData)
{
	FHapbeatEventRef Value = ReadValue();
	// Assign from the soft path rather than the loaded object: picking a map
	// should not force it resident just to record the reference.
	Value.EventMap = TSoftObjectPtr<UHapbeatEventMap>(AssetData.GetSoftObjectPath());
	// Entry ids are only meaningful inside the map that owns them, so a guid
	// carried over from the previous map could never resolve -- clear it and
	// make the author pick again.
	Value.EntryId = FGuid();
	WriteValue(Value);
	RefreshEntryOptions();
}

TSharedRef<SWidget> SHapbeatEventRefGraphPin::OnGenerateEntryWidget(TSharedPtr<FGuid> InId) const
{
	const FGuid Id = InId.IsValid() ? *InId : FGuid();
	return SNew(STextBlock)
		.Text(HapbeatEditor::DescribeEntryById(Id, ResolveEventMap()));
}

void SHapbeatEventRefGraphPin::OnEntrySelected(TSharedPtr<FGuid> NewSelection, ESelectInfo::Type /*SelectInfo*/)
{
	FHapbeatEventRef Value = ReadValue();
	Value.EntryId = NewSelection.IsValid() ? *NewSelection : FGuid();
	WriteValue(Value);
}

FText SHapbeatEventRefGraphPin::GetSelectedEntryLabel() const
{
	const FHapbeatEventRef Value = ReadValue();
	return HapbeatEditor::DescribeEntryById(Value.EntryId, Value.EventMap.LoadSynchronous());
}

TSharedRef<SWidget> SHapbeatEventRefGraphPin::GetDefaultValueWidget()
{
	RefreshEntryOptions();

	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SObjectPropertyEntryBox)
			.AllowedClass(UHapbeatEventMap::StaticClass())
			.ObjectPath(this, &SHapbeatEventRefGraphPin::GetEventMapPath)
			.OnObjectChanged(this, &SHapbeatEventRefGraphPin::OnEventMapChanged)
			.AllowClear(true)
			.DisplayUseSelected(true)
			.DisplayBrowse(true)
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(4.0f, 0.0f, 0.0f, 0.0f)
		[
			SAssignNew(EntryCombo, SComboBox<TSharedPtr<FGuid>>)
			.OptionsSource(&EntryOptions)
			// Rebuild on open so entries added to the map after this node was
			// drawn are offered without reopening the Blueprint.
			.OnComboBoxOpening(this, &SHapbeatEventRefGraphPin::RefreshEntryOptions)
			.OnGenerateWidget(this, &SHapbeatEventRefGraphPin::OnGenerateEntryWidget)
			.OnSelectionChanged(this, &SHapbeatEventRefGraphPin::OnEntrySelected)
			[
				SNew(STextBlock)
				.Text(this, &SHapbeatEventRefGraphPin::GetSelectedEntryLabel)
			]
		];
}

#undef LOCTEXT_NAMESPACE
