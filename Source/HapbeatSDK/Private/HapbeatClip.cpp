// Copyright (c) 2026 Hapbeat. MIT License.
#include "HapbeatClip.h"

DEFINE_LOG_CATEGORY_STATIC(LogHapbeatClip, Log, All);

namespace
{
	// Little-endian readers. Buf is the whole file; callers pre-validate that
	// [Pos, Pos+N) is in range before reading (see bounds checks in ParseWav).
	FORCEINLINE uint16 ReadU16LE(const uint8* Buf, int32 Pos)
	{
		return static_cast<uint16>(Buf[Pos]) |
			(static_cast<uint16>(Buf[Pos + 1]) << 8);
	}

	FORCEINLINE uint32 ReadU32LE(const uint8* Buf, int32 Pos)
	{
		return static_cast<uint32>(Buf[Pos]) |
			(static_cast<uint32>(Buf[Pos + 1]) << 8) |
			(static_cast<uint32>(Buf[Pos + 2]) << 16) |
			(static_cast<uint32>(Buf[Pos + 3]) << 24);
	}

	// FourCC compare against a 4-char ASCII literal (e.g. "RIFF"). Does not read
	// past the buffer: callers ensure Pos+4 <= Num first.
	FORCEINLINE bool FourCCEquals(const uint8* Buf, int32 Pos, const char (&Tag)[5])
	{
		return Buf[Pos + 0] == static_cast<uint8>(Tag[0]) &&
			Buf[Pos + 1] == static_cast<uint8>(Tag[1]) &&
			Buf[Pos + 2] == static_cast<uint8>(Tag[2]) &&
			Buf[Pos + 3] == static_cast<uint8>(Tag[3]);
	}
}

bool UHapbeatClip::ParseWav(const TArray<uint8>& WavBytes, int32& OutSampleRate, int32& OutChannels,
	TArray<uint8>& OutPcm16, FString& OutError)
{
	const uint8* Buf = WavBytes.GetData();
	const int32 Num = WavBytes.Num();

	// RIFF header: "RIFF"(4) + riffSize u32(4) + "WAVE"(4) = 12 bytes minimum.
	if (Num < 12)
	{
		OutError = FString::Printf(TEXT("WAV too small (%d bytes; need >= 12 for the RIFF header)."), Num);
		return false;
	}
	if (!FourCCEquals(Buf, 0, "RIFF"))
	{
		OutError = TEXT("Not a RIFF file (missing 'RIFF' magic).");
		return false;
	}
	if (!FourCCEquals(Buf, 8, "WAVE"))
	{
		OutError = TEXT("RIFF container is not 'WAVE'.");
		return false;
	}

	// Walk the chunk list that follows the 12-byte RIFF header. Each chunk is:
	//   id(4) + size u32(4) + data[size] + optional 1 pad byte if size is odd.
	// We don't trust the RIFF size field for the outer bound — we clamp to the
	// actual buffer length so a truncated / mis-sized file can't read OOB.
	bool bHaveFmt = false;
	bool bHaveData = false;
	int32 FmtSampleRate = 0;
	int32 FmtChannels = 0;
	int32 DataPos = 0;   // offset of the 'data' chunk's payload
	int32 DataSize = 0;  // byte length of the 'data' payload (clamped to buffer)

	int32 Pos = 12;
	while (Pos + 8 <= Num) // need a full 8-byte chunk header to proceed
	{
		const uint32 ChunkSize = ReadU32LE(Buf, Pos + 4);
		const int32 PayloadPos = Pos + 8;

		// Clamp the declared size to what is actually present so we never index
		// past the buffer (handles truncated files / bogus sizes gracefully).
		const int32 Avail = Num - PayloadPos;
		const int32 ClampedSize = static_cast<int32>(FMath::Min<int64>(static_cast<int64>(ChunkSize), static_cast<int64>(FMath::Max(Avail, 0))));

		if (FourCCEquals(Buf, Pos, "fmt "))
		{
			// fmt chunk core is 16 bytes: audioFormat u16, channels u16,
			// sampleRate u32, byteRate u32, blockAlign u16, bitsPerSample u16.
			if (ClampedSize < 16)
			{
				OutError = FString::Printf(TEXT("'fmt ' chunk too small (%d bytes present; need >= 16)."), ClampedSize);
				return false;
			}
			const uint16 AudioFormat = ReadU16LE(Buf, PayloadPos + 0);
			const uint16 Channels = ReadU16LE(Buf, PayloadPos + 2);
			const uint32 SampleRate = ReadU32LE(Buf, PayloadPos + 4);
			const uint16 BitsPerSample = ReadU16LE(Buf, PayloadPos + 14);

			// audioFormat 1 = PCM. (0xFFFE = WAVE_FORMAT_EXTENSIBLE would need the
			// SubFormat GUID checked; the kit pipeline always writes plain PCM16,
			// so we require the simple PCM form per the design constraint.)
			if (AudioFormat != 1)
			{
				OutError = FString::Printf(TEXT("Unsupported WAV audioFormat %u (only PCM = 1 is supported)."), AudioFormat);
				return false;
			}
			if (BitsPerSample != 16)
			{
				OutError = FString::Printf(TEXT("Unsupported WAV bit depth %u (only 16-bit PCM is supported)."), BitsPerSample);
				return false;
			}
			if (Channels == 0)
			{
				OutError = TEXT("WAV declares 0 channels.");
				return false;
			}
			if (SampleRate == 0)
			{
				OutError = TEXT("WAV declares a 0 sample rate.");
				return false;
			}

			FmtChannels = static_cast<int32>(Channels);
			FmtSampleRate = static_cast<int32>(SampleRate);
			bHaveFmt = true;
		}
		else if (FourCCEquals(Buf, Pos, "data"))
		{
			DataPos = PayloadPos;
			DataSize = ClampedSize;
			bHaveData = true;
		}
		// All other chunk ids (LIST / fact / cue / bext / id3 / ...) are skipped.

		// Advance to the next chunk. Use the *declared* size for the stride (so a
		// correctly-sized earlier chunk doesn't get cut short by a truncated
		// later one), but cap the final position at Num. Chunks are word-aligned:
		// if the size is odd there is a single trailing pad byte.
		int64 NextPos = static_cast<int64>(PayloadPos) + static_cast<int64>(ChunkSize);
		if ((ChunkSize & 1u) != 0)
		{
			NextPos += 1; // even-byte padding
		}

		// Guard against a zero-size chunk (would otherwise loop forever) and
		// against overflow/regression — always make forward progress.
		if (NextPos <= Pos)
		{
			NextPos = static_cast<int64>(Pos) + 8;
		}
		if (NextPos > Num)
		{
			break; // remaining declared bytes are not present; stop walking
		}
		Pos = static_cast<int32>(NextPos);
	}

	if (!bHaveFmt)
	{
		OutError = TEXT("WAV has no 'fmt ' chunk.");
		return false;
	}
	if (!bHaveData)
	{
		OutError = TEXT("WAV has no 'data' chunk.");
		return false;
	}

	// Trim a trailing odd byte so the PCM buffer is a whole number of int16
	// samples (a stray byte can't form a sample and would desync int16 reads).
	if ((DataSize & 1) != 0)
	{
		DataSize -= 1;
	}
	if (DataSize <= 0)
	{
		OutError = TEXT("WAV 'data' chunk is empty.");
		return false;
	}

	OutSampleRate = FmtSampleRate;
	OutChannels = FmtChannels;
	OutPcm16.SetNumUninitialized(DataSize);
	FMemory::Memcpy(OutPcm16.GetData(), Buf + DataPos, DataSize);
	return true;
}

UHapbeatClip* UHapbeatClip::CreateFromWavBytes(UObject* Outer, const TArray<uint8>& WavBytes)
{
	int32 SR = 0;
	int32 Ch = 0;
	TArray<uint8> Pcm;
	FString Error;
	if (!ParseWav(WavBytes, SR, Ch, Pcm, Error))
	{
		UE_LOG(LogHapbeatClip, Warning, TEXT("CreateFromWavBytes failed: %s"), *Error);
		return nullptr;
	}

	UHapbeatClip* Clip = NewObject<UHapbeatClip>(Outer ? Outer : GetTransientPackage());
	Clip->SampleRate = SR;
	Clip->NumChannels = Ch;
	Clip->Pcm16 = MoveTemp(Pcm);
	return Clip;
}
