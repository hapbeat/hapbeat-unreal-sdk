// Copyright (c) 2026 Hapbeat. MIT License.
#include "SHapbeatEntryRefGraphPin.h"

#include "HapbeatEntryPicker.h"
#include "HapbeatEventMap.h"

#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraph/EdGraphSchema.h"
#include "EdGraphSchema_K2.h"
#include "ScopedTransaction.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SHapbeatEntryRefGraphPin"

void SHapbeatEntryRefGraphPin::Construct(const FArguments& /*InArgs*/, UEdGraphPin* InGraphPinObj)
{
	SGraphPin::Construct(SGraphPin::FArguments(), InGraphPinObj);
}

FHapbeatEntryRef SHapbeatEntryRefGraphPin::ReadValue() const
{
	FHapbeatEntryRef Value;
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
	UScriptStruct* Struct = FHapbeatEntryRef::StaticStruct();
	Struct->ImportText(*DefaultString, &Value, /*OwnerObject*/ nullptr,
		PPF_SerializedAsImportText, /*ErrorText*/ GLog, Struct->GetName());
	return Value;
}

void SHapbeatEntryRefGraphPin::WriteValue(const FHapbeatEntryRef& NewValue)
{
	if (GraphPinObj == nullptr)
	{
		return;
	}
	UScriptStruct* Struct = FHapbeatEntryRef::StaticStruct();
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

	const FScopedTransaction Transaction(LOCTEXT("SetEntryRef", "Set Hapbeat Event"));
	GraphPinObj->Modify();
	GraphPinObj->GetSchema()->TrySetDefaultValue(*GraphPinObj, Exported);
}

UEdGraphPin* SHapbeatEntryRefGraphPin::FindEventMapPin() const
{
	if (GraphPinObj == nullptr)
	{
		return nullptr;
	}
	const UEdGraphNode* Node = GraphPinObj->GetOwningNodeUnchecked();
	if (Node == nullptr)
	{
		return nullptr;
	}
	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (Pin == nullptr || Pin->Direction != EGPD_Input
			|| Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Object)
		{
			continue;
		}
		// IsChildOf rather than == so a project that subclasses UHapbeatEventMap
		// still gets the picker.
		const UClass* PinClass = Cast<UClass>(Pin->PinType.PinSubCategoryObject.Get());
		if (PinClass != nullptr && PinClass->IsChildOf(UHapbeatEventMap::StaticClass()))
		{
			return Pin;
		}
	}
	return nullptr;
}

UHapbeatEventMap* SHapbeatEntryRefGraphPin::ResolveEventMap() const
{
	const UEdGraphPin* MapPin = FindEventMapPin();
	if (MapPin == nullptr || MapPin->LinkedTo.Num() > 0)
	{
		// A wired map is only known at runtime, so there is no entry list to
		// build here.
		return nullptr;
	}
	// The literal an object pin holds lives in DefaultObject (this is exactly
	// what a hard object reference buys us over a path in the default STRING).
	return Cast<UHapbeatEventMap>(MapPin->DefaultObject);
}

bool SHapbeatEntryRefGraphPin::IsEntryComboEnabled() const
{
	const UEdGraphPin* MapPin = FindEventMapPin();
	return MapPin == nullptr || MapPin->LinkedTo.Num() == 0;
}

void SHapbeatEntryRefGraphPin::RefreshEntryOptions()
{
	HapbeatEditor::BuildEntryOptions(ResolveEventMap(), EntryOptions);
	if (!EntryCombo.IsValid())
	{
		return;
	}
	EntryCombo->RefreshOptions();
	// Restate the selection from the value we hold. The list reconciles its
	// selection against the (rebuilt) options source, so anything it drops in
	// the process is put straight back instead of surfacing as an edit.
	// A guid the map no longer has falls back to the "(none)" row WITHOUT
	// writing -- the label still reads "(stale: xxxxxxxx)" from the stored value.
	EntryCombo->SetSelectedItem(HapbeatEditor::FindOptionForGuid(EntryOptions, ReadValue().EntryId));
}

TSharedRef<SWidget> SHapbeatEntryRefGraphPin::OnGenerateEntryWidget(TSharedPtr<FGuid> InId) const
{
	const FGuid Id = InId.IsValid() ? *InId : FGuid();
	return SNew(STextBlock)
		.Text(HapbeatEditor::DescribeEntryById(Id, ResolveEventMap()));
}

void SHapbeatEntryRefGraphPin::OnEntrySelected(TSharedPtr<FGuid> NewSelection, ESelectInfo::Type SelectInfo)
{
	if (SelectInfo == ESelectInfo::Direct)
	{
		// Not the author picking anything: Direct is what the list reports for
		// programmatic selection -- our own SetSelectedItem, and the null
		// selection it emits when a rebuilt options list makes it drop the
		// current item. Writing here is exactly how opening the dropdown used to
		// wipe the pin back to "(none)". User input arrives as OnMouseClick /
		// OnKeyPress / OnNavigation instead.
		return;
	}

	FHapbeatEntryRef Value = ReadValue();
	Value.EntryId = NewSelection.IsValid() ? *NewSelection : FGuid();
	WriteValue(Value);
}

FText SHapbeatEntryRefGraphPin::GetSelectedEntryLabel() const
{
	const UEdGraphPin* MapPin = FindEventMapPin();
	if (MapPin != nullptr && MapPin->LinkedTo.Num() > 0)
	{
		return LOCTEXT("MapIsLinked", "(map is linked)");
	}
	return HapbeatEditor::DescribeEntryById(ReadValue().EntryId, ResolveEventMap());
}

TSharedRef<SWidget> SHapbeatEntryRefGraphPin::GetDefaultValueWidget()
{
	RefreshEntryOptions();

	return SAssignNew(EntryCombo, SComboBox<TSharedPtr<FGuid>>)
		.OptionsSource(&EntryOptions)
		.IsEnabled(this, &SHapbeatEntryRefGraphPin::IsEntryComboEnabled)
		// Rebuild on open: this widget is never told when the neighbouring map
		// pin changes, so opening the dropdown is the moment we re-read it.
		// Deliberately NOT clearing a now-stale entry id when the map changes --
		// that would need a custom K2Node to hook the map pin's change, and a
		// stale id is already visible as "(stale: xxxxxxxx)" in the label.
		.OnComboBoxOpening(this, &SHapbeatEntryRefGraphPin::RefreshEntryOptions)
		.OnGenerateWidget(this, &SHapbeatEntryRefGraphPin::OnGenerateEntryWidget)
		.OnSelectionChanged(this, &SHapbeatEntryRefGraphPin::OnEntrySelected)
		[
			SNew(STextBlock)
			.Text(this, &SHapbeatEntryRefGraphPin::GetSelectedEntryLabel)
		];
}

#undef LOCTEXT_NAMESPACE
