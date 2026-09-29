// Copyright (c) 2026 Hapbeat. MIT License.
#include "HapbeatLog.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY(LogHapbeat);

// Runtime-only module with no special startup/shutdown work.
IMPLEMENT_MODULE(FDefaultModuleImpl, HapbeatSDK);
