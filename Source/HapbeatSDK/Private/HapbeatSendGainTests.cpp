// Copyright (c) 2026 Hapbeat. MIT License.
#include "HapbeatEventEntry.h"
#include "HapbeatStreamPlayback.h"

#include "Misc/AutomationTest.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
	const float NaN = std::numeric_limits<float>::quiet_NaN();
	const float Inf = std::numeric_limits<float>::infinity();

	/** An entry with the given Gain and baked clip gain (negative = unresolved / no Kit). */
	FHapbeatEventEntry MakeEntry(float Gain, float ClipGain)
	{
		FHapbeatEventEntry Entry;
		Entry.Gain = Gain;
		Entry.CachedManifestClipGain = ClipGain;
		return Entry;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHapbeatSendGainTest,
	"Hapbeat.Gain.SendGainFormula",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHapbeatSendGainTest::RunTest(const FString& Parameters)
{
	// The value sent is clamp(clip_gain x app_gain, 0, 1) (contracts
	// kit-format.md, the strength formula). app_gain here is entry Gain x the
	// call-site gain; only the final product is capped. Checked on both the
	// Command path (GetSendGain -> the PLAY gain) and the Stream Clip path
	// (UHapbeatStreamPlayback::Gain, the factor the source PCM is scaled by).
	struct FCase
	{
		const TCHAR* Name;
		float EntryGain;
		float ClipGain; // < 0 = unresolved (no Kit): treated as 1
		float CallGain;
		float Expected;
	};
	const FCase Cases[] = {
		{TEXT("clip_gain 0.5 x gain 1"), 1.0f, 0.5f, 1.0f, 0.5f},
		{TEXT("clip_gain 0.5 x gain 2"), 1.0f, 0.5f, 2.0f, 1.0f},
		{TEXT("clip_gain 0.8 x gain 2 caps at 1"), 1.0f, 0.8f, 2.0f, 1.0f},
		{TEXT("clip_gain 0.5 x gain 0"), 1.0f, 0.5f, 0.0f, 0.0f},
		{TEXT("no Kit (clip_gain unknown) x gain 1.5"), 1.0f, -1.0f, 1.5f, 1.0f},
		{TEXT("entry gain 1.5 x call gain 0.5 x clip_gain 0.8"), 1.5f, 0.8f, 0.5f, 0.6f},
		{TEXT("negative call gain floors at 0"), 1.0f, 0.5f, -1.0f, 0.0f},
		{TEXT("NaN call gain sends 0"), 1.0f, 0.5f, NaN, 0.0f},
		{TEXT("+Inf call gain sends 0"), 1.0f, 0.5f, Inf, 0.0f},
		{TEXT("-Inf call gain sends 0"), 1.0f, 0.5f, -Inf, 0.0f},
		{TEXT("NaN clip_gain sends 0"), 1.0f, NaN, 1.0f, 0.0f},
	};

	for (const FCase& Case : Cases)
	{
		const FHapbeatEventEntry Entry = MakeEntry(Case.EntryGain, Case.ClipGain);

		TestEqual(FString::Printf(TEXT("command: %s"), Case.Name),
			Entry.GetSendGain(Case.CallGain), Case.Expected, KINDA_SMALL_NUMBER);

		// Stream Clip: PlayEntry hands StreamClip the entry's effective gain as
		// the baseline and the call-site gain as the initial modulator.
		UHapbeatStreamPlayback* Playback = NewObject<UHapbeatStreamPlayback>();
		Playback->Init(Entry.GetEffectiveGain(), Case.CallGain);
		TestEqual(FString::Printf(TEXT("stream start: %s"), Case.Name),
			Playback->GetGain(), Case.Expected, KINDA_SMALL_NUMBER);
		TestEqual(FString::Printf(TEXT("stream mirror: %s"), Case.Name),
			Playback->GetMirror()->Gain.load(std::memory_order_relaxed), Case.Expected, KINDA_SMALL_NUMBER);

		// A binding / live multiplier re-applies the modulator through the same formula.
		Playback->ApplyGainModulation(1.0f);
		Playback->ApplyGainModulation(Case.CallGain);
		TestEqual(FString::Printf(TEXT("stream modulation: %s"), Case.Name),
			Playback->GetGain(), Case.Expected, KINDA_SMALL_NUMBER);
	}

	// The raw Play(event id, gain) path has no clip_gain (= 1): only the cap applies.
	TestEqual(TEXT("raw gain 1.5 caps at 1"), FHapbeatEventEntry::ClampSendGain(1.5f), 1.0f);
	TestEqual(TEXT("raw gain 0.25 passes"), FHapbeatEventEntry::ClampSendGain(0.25f), 0.25f);
	TestEqual(TEXT("raw negative gain floors at 0"), FHapbeatEventEntry::ClampSendGain(-0.5f), 0.0f);
	TestEqual(TEXT("raw NaN sends 0"), FHapbeatEventEntry::ClampSendGain(NaN), 0.0f);
	TestEqual(TEXT("raw +Inf sends 0"), FHapbeatEventEntry::ClampSendGain(Inf), 0.0f);
	TestEqual(TEXT("raw -Inf sends 0"), FHapbeatEventEntry::ClampSendGain(-Inf), 0.0f);
	return true;
}
#endif // WITH_DEV_AUTOMATION_TESTS
