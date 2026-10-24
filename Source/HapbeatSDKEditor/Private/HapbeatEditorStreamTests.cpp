// Copyright (c) 2026 Hapbeat. MIT License.
#include "Misc/AutomationTest.h"
#include "HapbeatEditorSender.h"
#include "HapbeatStreamRunnable.h"

#if WITH_DEV_AUTOMATION_TESTS

// Editor Test Play (no socket, nothing sent): which devices a stream Test Play
// opens a session to, in which format, and what each session's BEGIN carries.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHapbeatGen2EditorTestPlayTest,
	"Hapbeat.Streaming.Gen2.EditorTestPlay", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHapbeatGen2EditorTestPlayTest::RunTest(const FString& Parameters)
{
	using FDevice = FHapbeatEditorSender::FEditorDevice;
	using FPlan = FHapbeatEditorSender::FEditorStreamPlan;

	const uint64 Incarnation = 0x1122334455667788ULL;
	const double Now = 1000.0;
	// A boot id no other test uses: generations are allocated per lease for the
	// editor's whole run.
	const FGuid Guid = FGuid::NewGuid();
	const uint64 BootId = (static_cast<uint64>(Guid.A) << 32) | Guid.B | 1ULL;

	auto MakeGen2 = [&](const TCHAR* Ip, const TCHAR* Address, uint32 Ticket, uint64 LeaseIncarnation, bool bSuperseded)
	{
		FDevice Device;
		Device.Ip = Ip;
		Device.Port = 7700;
		Device.Address = Address;
		Device.LastPongSeconds = Now - 1.0;
		Device.LeaseState.Class = EHapbeatStreamEndpointClass::V2;
		Device.LeaseState.Lease = FHapbeatStreamLeaseIdentity(BootId, Ticket);
		Device.LeaseState.bLeaseValid = !bSuperseded;
		Device.LeaseState.bLeaseSuperseded = bSuperseded;
		Device.LeaseState.ClientIncarnation = LeaseIncarnation;
		return Device;
	};
	auto MakeGen1 = [&](const TCHAR* Ip, const TCHAR* Address, double LastPong)
	{
		FDevice Device;
		Device.Ip = Ip;
		Device.Port = 7700;
		Device.Address = Address;
		Device.LastPongSeconds = LastPong;
		Device.LeaseState.Class = EHapbeatStreamEndpointClass::Legacy;
		Device.LeaseState.ClientIncarnation = Incarnation;
		return Device;
	};

	TArray<FDevice> Devices;
	Devices.Add(MakeGen2(TEXT("10.0.0.2"), TEXT("p1/g1"), 7, Incarnation, false));
	Devices.Add(MakeGen1(TEXT("10.0.0.3"), TEXT("p1/g2"), Now - 1.0));
	// Address does not match the target.
	Devices.Add(MakeGen2(TEXT("10.0.0.4"), TEXT("p2/g1"), 8, Incarnation, false));
	// Not classified yet.
	FDevice Unknown;
	Unknown.Ip = TEXT("10.0.0.5");
	Unknown.Port = 7700;
	Unknown.Address = TEXT("p1/g3");
	Unknown.LastPongSeconds = Now - 1.0;
	Devices.Add(Unknown);
	// Another sender holds it.
	Devices.Add(MakeGen2(TEXT("10.0.0.6"), TEXT("p1/g4"), 9, Incarnation, true));
	// Lease of an earlier incarnation of this editor.
	Devices.Add(MakeGen2(TEXT("10.0.0.7"), TEXT("p1/g5"), 10, Incarnation + 1, false));
	// Not heard from for longer than the device TTL.
	Devices.Add(MakeGen1(TEXT("10.0.0.8"), TEXT("p1/g6"), Now - 20.0));

	const TArray<FPlan> Plans = FHapbeatEditorSender::PlanStreamSessions(Devices, TEXT("p1"), Incarnation, Now);
	if (!TestEqual(TEXT("one session per matching streamable device"), Plans.Num(), 2))
	{
		return false;
	}
	TestEqual(TEXT("Gen 2 device ip"), Plans[0].Ip, FString(TEXT("10.0.0.2")));
	TestTrue(TEXT("Gen 2 device gets Stream Gen 2"), Plans[0].Format == EHapbeatStreamWireFormat::SessionV2);
	TestTrue(TEXT("Gen 2 session under the device's lease"), Plans[0].Lease == FHapbeatStreamLeaseIdentity(BootId, 7));
	TestEqual(TEXT("Gen 2 BEGIN targets the device address"), Plans[0].Address, FString(TEXT("p1/g1")));
	TestEqual(TEXT("Gen 1 device ip"), Plans[1].Ip, FString(TEXT("10.0.0.3")));
	TestTrue(TEXT("Gen 1 device gets Stream Gen 1"), Plans[1].Format == EHapbeatStreamWireFormat::LegacyV1);
	TestEqual(TEXT("Gen 1 BEGIN targets the device address"), Plans[1].Address, FString(TEXT("p1/g2")));

	// Empty target: every live streamable device, matching or not.
	const TArray<FPlan> AllPlans = FHapbeatEditorSender::PlanStreamSessions(Devices, TEXT(""), Incarnation, Now);
	TestEqual(TEXT("empty target reaches every streamable device"), AllPlans.Num(), 3);

	// The sessions built from the plans (not started, so nothing is sent).
	TArray<uint8> Pcm;
	Pcm.SetNumZeroed(16000 / 10 * 2 * 2);
	auto Mirror = MakeShared<FHapbeatStreamGainMirror, ESPMode::ThreadSafe>();

	FHapbeatStreamRunnable* Gen2 = FHapbeatEditorSender::CreateSessionRunnable(
		Plans[0], TArray<uint8>(Pcm), 16000, 2, Mirror, nullptr, 0.05f);
	FHapbeatStreamRunnable* Gen1 = FHapbeatEditorSender::CreateSessionRunnable(
		Plans[1], TArray<uint8>(Pcm), 16000, 2, Mirror, nullptr, 0.05f);
	if (TestNotNull(TEXT("Gen 2 session"), Gen2))
	{
		TestTrue(TEXT("Gen 2 session wire format"), Gen2->GetWireFormat() == EHapbeatStreamWireFormat::SessionV2);
		TestTrue(TEXT("Gen 2 session begins at generation 1 of the lease"),
			Gen2->GetSessionIdentity() == FHapbeatStreamSessionIdentity(FHapbeatStreamLeaseIdentity(BootId, 7), 1));
		TestTrue(TEXT("Gen 2 session target is the device address"), Gen2->IsCompatible(16000, 2, TEXT("p1/g1")));
	}
	if (TestNotNull(TEXT("Gen 1 session"), Gen1))
	{
		TestTrue(TEXT("Gen 1 session wire format"), Gen1->GetWireFormat() == EHapbeatStreamWireFormat::LegacyV1);
		TestFalse(TEXT("Gen 1 session carries no envelope"), Gen1->GetSessionIdentity().IsValid());
		TestTrue(TEXT("Gen 1 session target is the device address"), Gen1->IsCompatible(16000, 2, TEXT("p1/g2")));
	}

	// A second Test Play on the same lease takes the next generation.
	FHapbeatStreamRunnable* Gen2Again = FHapbeatEditorSender::CreateSessionRunnable(
		Plans[0], TArray<uint8>(Pcm), 16000, 2, Mirror, nullptr, 0.05f);
	if (TestNotNull(TEXT("second Gen 2 session"), Gen2Again))
	{
		TestEqual(TEXT("generation is never reused"), Gen2Again->GetSessionIdentity().Generation, 2u);
	}

	delete Gen2;
	delete Gen1;
	delete Gen2Again;
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
