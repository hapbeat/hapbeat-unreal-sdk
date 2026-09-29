// Copyright (c) 2026 Hapbeat. MIT License.
#pragma once

#include "CoreMinimal.h"

// Shared editor log category, defined once in HapbeatSDKEditorModule.cpp so the
// module stays unity-build safe (see HapbeatSDK/Private/HapbeatLog.h).
DECLARE_LOG_CATEGORY_EXTERN(LogHapbeatEditor, Log, All);
