/*++

Module Name:

    VsLoopback.h

Abstract:

    VoiceStudio Virtual Microphone: the shared buffer that carries the audio VoiceStudio plays into the
    "VoiceStudio Mic Feed" render endpoint to the "VoiceStudio Virtual Microphone" capture endpoint.

    Both endpoints run on the same clock (the stream position timers derive from KeQueryPerformanceCounter),
    so there is no drift to correct: the capture side only keeps a small, constant distance behind the
    render side. The buffer holds 48 kHz mono 16-bit samples; render streams are mixed down to mono.

    Called from the WaveRT position updates (DISPATCH_LEVEL or below); protected by a spin lock.

--*/

#ifndef _VOICESTUDIO_LOOPBACK_H_
#define _VOICESTUDIO_LOOPBACK_H_

// Per capture stream: where it reads, and whether it is waiting for enough audio to start (again).
typedef struct _VS_LOOPBACK_READER
{
    ULONGLONG   Position;   // in samples since the driver loaded
    ULONGLONG   PrimeFrom;  // while priming: only audio written after this point counts (never replay old audio)
    BOOLEAN     Priming;
} VS_LOOPBACK_READER, *PVS_LOOPBACK_READER;

NTSTATUS VsLoopbackInitialize();
VOID     VsLoopbackCleanup();

// Render side: appends `Bytes` of PCM in `Format` (16-bit, any channel count, 48 kHz).
VOID VsLoopbackWrite(_In_reads_bytes_(Bytes) const BYTE* Data, _In_ ULONG Bytes, _In_ const WAVEFORMATEX* Format);

// Capture side: fills `Bytes` of PCM in `Format` (16-bit, any channel count, 48 kHz) from the buffer;
// silence while nothing is being played.
VOID VsLoopbackRead(_Out_writes_bytes_(Bytes) BYTE* Data, _In_ ULONG Bytes, _In_ const WAVEFORMATEX* Format,
                    _Inout_ PVS_LOOPBACK_READER Reader);

VOID VsLoopbackResetReader(_Out_ PVS_LOOPBACK_READER Reader);

#endif // _VOICESTUDIO_LOOPBACK_H_
