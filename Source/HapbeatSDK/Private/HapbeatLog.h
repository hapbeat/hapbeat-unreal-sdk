// Copyright (c) 2026 Hapbeat. MIT License.
#pragma once

#include "CoreMinimal.h"

// Shared runtime log category, defined once in HapbeatSDKModule.cpp. Per-file
// DEFINE_LOG_CATEGORY_STATIC(LogHapbeat, ...) breaks unity builds (C2011) when
// UBT groups those .cpp files into one translation unit.
DECLARE_LOG_CATEGORY_EXTERN(LogHapbeat, Log, All);
