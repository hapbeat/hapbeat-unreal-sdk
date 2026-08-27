// Copyright (c) 2026 Hapbeat. MIT License.
#pragma once

#include "CoreMinimal.h"
#include <atomic>

/**
 * Thread-safe value mirror of a UHapbeatStreamPlayback's live Gain / Pan /
 * Loop / stopped state, shared between the game thread (writer:
 * ApplyGainModulation / SetPan / SetLoop / Stop) and the
 * dedicated stream-send thread (reader: FHapbeatStreamRunnable, every chunk).
 *
 * Exists so the stream thread NEVER touches the UHapbeatStreamPlayback UObject
 * itself — dereferencing / reading a UObject from a non-game thread is unsafe
 * with respect to garbage collection (the GC can run concurrently on the game
 * thread and reclaim the object while a worker thread is mid-read). Plain
 * struct (no UObject, no UCLASS); the playback handle and the streamer both
 * hold a TSharedRef<..., ThreadSafe> to the SAME instance so there is no
 * ownership ambiguity across the object's / session's lifetime.
 *
 * Lives in Public/ (not Private/) because it appears in the signature of the
 * Public UHapbeatStreamPlayback::GetMirror().
 *
 * All four fields are lock-free (std::atomic), single-writer (game thread)
 * per field / single-reader (stream thread) per read — no torn reads, no lock
 * needed for this simple value-mirror pattern.
 */
struct FHapbeatStreamGainMirror
{
	std::atomic<float> Gain{1.0f};
	std::atomic<float> Pan{0.0f};
	std::atomic<bool> bLoop{false};
	std::atomic<bool> bStopped{false};
};
