// Copyright (c) 2026 Hapbeat. MIT License.
#pragma once

#include "CoreMinimal.h"
#include "EdGraphUtilities.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "HapbeatEntryRef.h"
#include "SHapbeatEntryRefGraphPin.h"

/**
 * Substitutes SHapbeatEntryRefGraphPin for the default struct pin widget on
 * every FHapbeatEntryRef pin in any Blueprint graph.
 *
 * Registered globally (FEdGraphUtilities::RegisterVisualPinFactory) by
 * FHapbeatSDKEditorModule, which also keeps the instance alive -- unregistering
 * needs the same shared pointer. Structurally identical to the engine's
 * FGameplayTagsGraphPanelPinFactory.
 */
class FHapbeatEntryRefPinFactory : public FGraphPanelPinFactory
{
	virtual TSharedPtr<SGraphPin> CreatePin(UEdGraphPin* InPin) const override
	{
		if (InPin == nullptr || InPin->PinType.PinCategory != UEdGraphSchema_K2::PC_Struct)
		{
			return nullptr;
		}
		// IsChildOf rather than == so a future struct deriving from
		// FHapbeatEntryRef inherits the picker instead of silently falling back
		// to the raw struct pin.
		const UScriptStruct* PinStruct = Cast<UScriptStruct>(InPin->PinType.PinSubCategoryObject.Get());
		if (PinStruct != nullptr && PinStruct->IsChildOf(FHapbeatEntryRef::StaticStruct()))
		{
			return SNew(SHapbeatEntryRefGraphPin, InPin);
		}
		return nullptr;
	}
};
