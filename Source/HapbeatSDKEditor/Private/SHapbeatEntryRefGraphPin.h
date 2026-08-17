// Copyright (c) 2026 Hapbeat. MIT License.
#pragma once

#include "CoreMinimal.h"
#include "HapbeatEntryRef.h"
#include "SGraphPin.h"

class SWidget;
class UHapbeatEventMap;
template <typename OptionType> class SComboBox;

/**
 * Graph pin widget for FHapbeatEntryRef: a dropdown of the Event Map's entries
 * BY NAME, drawn inline on the node.
 *
 * This is the whole reason FHapbeatEntryRef exists -- the value stored in the
 * pin is the entry's stable GUID, which no author could ever type, so the pin
 * has to render the name instead. Same engine mechanism as the GameplayTag pin:
 * a FGraphPanelPinFactory matches the pin's struct type and returns this widget
 * in place of the default struct pin (which for a nested struct is just a
 * "(...)"-style text field).
 *
 * There is NO map picker here. The map is the neighbouring OBJECT pin of the
 * same node (see HapbeatEntryRef.h for why it must be a real object pin), and
 * this widget only READS it to know which entries to offer -- exactly how
 * K2Node_GetDataTableRow's RowName pin reads its DataTable pin.
 *
 * The pin default is a plain STRING (that is how UEdGraphPin serializes struct
 * defaults), so every read/write goes through UScriptStruct Import/ExportText.
 * Unlike the previous map+guid struct, this round-trips reliably: a GUID is
 * pure data with no object reference in it.
 */
class SHapbeatEntryRefGraphPin : public SGraphPin
{
public:
	SLATE_BEGIN_ARGS(SHapbeatEntryRefGraphPin) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UEdGraphPin* InGraphPinObj);

protected:
	/** The inline editor shown when the pin is unconnected (SGraphPin hides it once something is plugged in). */
	virtual TSharedRef<SWidget> GetDefaultValueWidget() override;

private:
	/** Parse the pin's default string into a struct value. Returns a default-constructed value for an empty / unparseable default. */
	FHapbeatEntryRef ReadValue() const;

	/** Serialize the struct back into the pin default, through the schema so the graph is marked dirty and undo works. No-op when nothing changed. */
	void WriteValue(const FHapbeatEntryRef& NewValue);

	/**
	 * The Event Map pin sitting next to this one on the same node.
	 *
	 * Found BY TYPE (an input object pin whose class is UHapbeatEventMap), not
	 * by pin name: the name belongs to the function signature, so matching on
	 * "Map" would silently stop working the moment a parameter is renamed, or
	 * for any future node that spells it differently. The type is the actual
	 * contract.
	 */
	UEdGraphPin* FindEventMapPin() const;

	/** Load the map the neighbouring pin currently holds. Null when unset, linked (a runtime value we cannot read here), or missing. */
	UHapbeatEventMap* ResolveEventMap() const;

	/** True while the map pin holds a literal we can read; false when it is wired up, which is when the dropdown has nothing to list. */
	bool IsEntryComboEnabled() const;

	/**
	 * Rebuild EntryOptions from the map pin's CURRENT value. Called on every
	 * combo open, which is also how the dropdown follows a map picked after this
	 * node was drawn -- the pin widget gets no notification when its neighbour
	 * changes.
	 */
	void RefreshEntryOptions();

	TSharedRef<SWidget> OnGenerateEntryWidget(TSharedPtr<FGuid> InId) const;
	void OnEntrySelected(TSharedPtr<FGuid> NewSelection, ESelectInfo::Type SelectInfo);
	FText GetSelectedEntryLabel() const;

	/** Index 0 is always the "(none)" invalid guid (see HapbeatEditor::BuildEntryOptions). */
	TArray<TSharedPtr<FGuid>> EntryOptions;
	TSharedPtr<SComboBox<TSharedPtr<FGuid>>> EntryCombo;
};
