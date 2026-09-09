// Copyright (c) 2026 Hapbeat. MIT License.
#pragma once

/** Editor-only commands that author Blueprint-complete Showcase assets. */
namespace HapbeatShowcaseBlueprintBuilder
{
void Generate();
void GenerateDoorAsset();
void GenerateStreamConsoleAssets();
/** Add instance-editable Event Map / Entry variables to the existing Z2 graph without moving or rebuilding its nodes. */
void EnableRealtimeDoorEventOverrides();
}
