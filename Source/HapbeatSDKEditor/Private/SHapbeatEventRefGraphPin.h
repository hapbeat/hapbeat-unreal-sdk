// Copyright (c) 2026 Hapbeat. MIT License.
#pragma once

#include "CoreMinimal.h"
#include "HapbeatEventRef.h"
#include "SGraphPin.h"

class SWidget;
class UHapbeatEventMap;
struct FAssetData;
template <typename OptionType> class SComboBox;

/**
 * Graph pin widget for FHapbeatEventRef: an Event Map asset picker plus a
 * dropdown of that map's entries BY NAME, drawn inline on the node.
 *
 * This is the whole reason FHapbeatEventRef exists -- the value stored in the
 * pin is still the entry's stable GUID, which no author could ever type, so
 * the pin has to render the name instead. Same approach (and same engine
 * mechanism) as the GameplayTag pin: a FGraphPanelPinFactory matches the pin's
 * struct type and returns this widget in place of the default struct pin,
 * which for a nested struct is just a "(...)"-style text field.
 *
 * The pin default is a plain STRING (that is how UEdGraphPin serializes struct
 * defaults), so every read/write goes through UScriptStruct Import/ExportText.
 */
class SHapbeatEventRefGraphPin : public SGraphPin
{
public:
	SLATE_BEGIN_ARGS(SHapbeatEventRefGraphPin) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UEdGraphPin* InGraphPinObj);

protected:
	/** The inline editor shown when the pin is unconnected (SGraphPin hides it once something is plugged in). */
	virtual TSharedRef<SWidget> GetDefaultValueWidget() override;

private:
	/** Parse the pin's default string into a struct value. Returns a default-constructed value for an empty / unparseable default. */
	FHapbeatEventRef ReadValue() const;

	/** Serialize the struct back into the pin default, through the schema so the graph is marked dirty and undo works. No-op when nothing changed. */
	void WriteValue(const FHapbeatEventRef& NewValue);

	/** Load the referenced Event Map (editor-side, so a synchronous load is acceptable). Null when unset or missing. */
	UHapbeatEventMap* ResolveEventMap() const;

	/**
	 * Rebuild EntryOptions from the currently referenced map. Also called when
	 * the combo opens, so entries added to the map after this node was drawn
	 * show up without reopening the Blueprint.
	 */
	void RefreshEntryOptions();

	FString GetEventMapPath() const;
	void OnEventMapChanged(const FAssetData& AssetData);

	TSharedRef<SWidget> OnGenerateEntryWidget(TSharedPtr<FGuid> InId) const;
	void OnEntrySelected(TSharedPtr<FGuid> NewSelection, ESelectInfo::Type SelectInfo);
	FText GetSelectedEntryLabel() const;

	/** Index 0 is always the "(none)" invalid guid (see HapbeatEditor::BuildEntryOptions). */
	TArray<TSharedPtr<FGuid>> EntryOptions;
	TSharedPtr<SComboBox<TSharedPtr<FGuid>>> EntryCombo;
};
