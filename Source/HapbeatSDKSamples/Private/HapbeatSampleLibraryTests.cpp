// Copyright (c) 2026 Hapbeat. MIT License.
#include "HapbeatSampleLibrary.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Pins down FHapbeatSampleLibrary::UnityEulerToUERotator, the one place where a
 * sign error would be invisible in code review and obvious only once a rod or a
 * blaster is on screen pointing the wrong way.
 *
 * The expected values are the axis-by-axis rule the matrix composition works
 * out to -- Pitch = -UnityX, Yaw = +UnityY, Roll = -UnityZ. The rule holds for
 * composites too (Unity's ZXY order maps onto UE's Roll-Pitch-Yaw order
 * one-for-one under the axis permutation), which is what the fourth case
 * checks; the implementation still goes through the matrix so that a future
 * change to either engine's convention breaks the test rather than the scene.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHapbeatUnityEulerToUERotatorTest,
	"Hapbeat.Samples.UnityEulerToUERotator",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHapbeatUnityEulerToUERotatorTest::RunTest(const FString& Parameters)
{
	auto Check = [this](const TCHAR* What, const FVector& UnityEuler, const FRotator& Expected)
	{
		const FRotator Actual = FHapbeatSampleLibrary::UnityEulerToUERotator(UnityEuler);
		// Compare as quaternions: (0, 0, 180) and (180, 180, 0) are the same
		// orientation, and FMatrix::Rotator() is free to return either.
		if (!Actual.Quaternion().Equals(Expected.Quaternion(), 1.0e-4f))
		{
			AddError(FString::Printf(TEXT("%s: Unity %s -> got %s, expected %s"),
				What, *UnityEuler.ToString(), *Actual.ToString(), *Expected.ToString()));
		}
	};

	// Yaw keeps its sign: Unity's Y euler and UE's Yaw are both right-handed
	// about the same (permuted) axis.
	Check(TEXT("yaw"), FVector(0.0f, 90.0f, 0.0f), FRotator(0.0f, 90.0f, 0.0f));
	// Pitch flips: Unity's +X euler pitches the nose DOWN, UE's +Pitch lifts it.
	Check(TEXT("pitch"), FVector(90.0f, 0.0f, 0.0f), FRotator(-90.0f, 0.0f, 0.0f));
	// Roll flips for the same reason (UE's Roll is left-handed about +X).
	Check(TEXT("roll"), FVector(0.0f, 0.0f, 90.0f), FRotator(0.0f, 0.0f, -90.0f));
	// The Z3 rod's authored CameraFollowMount offset -- the composite this
	// conversion actually exists for.
	Check(TEXT("rod mount"), FVector(-17.5f, 16.83f, 8.92f), FRotator(17.5f, 16.83f, -8.92f));
	// An arbitrary composite, so the order-of-composition claim is not only
	// tested on a value that happens to be small.
	Check(TEXT("composite"), FVector(30.0f, 40.0f, 50.0f), FRotator(-30.0f, 40.0f, -50.0f));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
