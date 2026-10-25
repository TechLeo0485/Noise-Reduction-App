/*++

Module Name:

    VsLoopback.cpp

Abstract:

    VoiceStudio Virtual Microphone: render -> capture loopback buffer. See VsLoopback.h.

--*/

#include <sysvad.h>
#include "VsLoopback.h"

#define VS_LOOPBACK_POOLTAG     'kLsV'
#define VS_LOOPBACK_RATE        48000
#define VS_LOOPBACK_SAMPLES     (VS_LOOPBACK_RATE * 2)     // 2 s of mono 16-bit audio
#define VS_LOOPBACK_TARGET      (VS_LOOPBACK_RATE / 50)    // 20 ms: covers the jitter of both position timers
#define VS_LOOPBACK_MAX_DELAY   (VS_LOOPBACK_RATE / 10)    // above 100 ms behind, skip back to the target

static KSPIN_LOCK   g_VsLoopbackLock;
static SHORT*       g_VsLoopbackBuffer = NULL;
static ULONGLONG    g_VsLoopbackWritten = 0;               // samples written since the driver loaded

#pragma code_seg("INIT")
NTSTATUS VsLoopbackInitialize()
{
    KeInitializeSpinLock(&g_VsLoopbackLock);
    g_VsLoopbackBuffer = (SHORT*)ExAllocatePool2(POOL_FLAG_NON_PAGED, VS_LOOPBACK_SAMPLES * sizeof(SHORT),
                                                  VS_LOOPBACK_POOLTAG);
    g_VsLoopbackWritten = 0;
    return g_VsLoopbackBuffer != NULL ? STATUS_SUCCESS : STATUS_INSUFFICIENT_RESOURCES;
}

#pragma code_seg("PAGE")
VOID VsLoopbackCleanup()
{
    PAGED_CODE();
    if (g_VsLoopbackBuffer != NULL)
    {
        ExFreePoolWithTag(g_VsLoopbackBuffer, VS_LOOPBACK_POOLTAG);
        g_VsLoopbackBuffer = NULL;
    }
}

#pragma code_seg()
static BOOLEAN VsLoopbackFormatOk(_In_ const WAVEFORMATEX* Format)
{
    return Format != NULL && Format->wBitsPerSample == 16 && Format->nChannels >= 1 &&
           Format->nBlockAlign == Format->nChannels * sizeof(SHORT) && Format->nSamplesPerSec == VS_LOOPBACK_RATE;
}

#pragma code_seg()
VOID VsLoopbackWrite(_In_reads_bytes_(Bytes) const BYTE* Data, _In_ ULONG Bytes, _In_ const WAVEFORMATEX* Format)
{
    if (g_VsLoopbackBuffer == NULL || !VsLoopbackFormatOk(Format))
    {
        return;
    }
    const ULONG channels = Format->nChannels;
    const ULONG frames = Bytes / Format->nBlockAlign;
    const SHORT* in = (const SHORT*)Data;

    KIRQL irql;
    KeAcquireSpinLock(&g_VsLoopbackLock, &irql);
    for (ULONG f = 0; f < frames; ++f)
    {
        LONG sum = 0;
        for (ULONG c = 0; c < channels; ++c)
        {
            sum += in[f * channels + c];
        }
        g_VsLoopbackBuffer[(g_VsLoopbackWritten + f) % VS_LOOPBACK_SAMPLES] = (SHORT)(sum / (LONG)channels);
    }
    g_VsLoopbackWritten += frames;
    KeReleaseSpinLock(&g_VsLoopbackLock, irql);
}

#pragma code_seg()
VOID VsLoopbackResetReader(_Out_ PVS_LOOPBACK_READER Reader)
{
    KIRQL irql;
    KeAcquireSpinLock(&g_VsLoopbackLock, &irql);
    Reader->Position = g_VsLoopbackWritten;
    Reader->PrimeFrom = g_VsLoopbackWritten;
    Reader->Priming = TRUE;
    KeReleaseSpinLock(&g_VsLoopbackLock, irql);
}

#pragma code_seg()
VOID VsLoopbackRead(_Out_writes_bytes_(Bytes) BYTE* Data, _In_ ULONG Bytes, _In_ const WAVEFORMATEX* Format,
                    _Inout_ PVS_LOOPBACK_READER Reader)
{
    RtlZeroMemory(Data, Bytes);
    if (g_VsLoopbackBuffer == NULL || !VsLoopbackFormatOk(Format))
    {
        return;
    }
    const ULONG channels = Format->nChannels;
    const ULONG frames = Bytes / Format->nBlockAlign;
    SHORT* out = (SHORT*)Data;

    KIRQL irql;
    KeAcquireSpinLock(&g_VsLoopbackLock, &irql);
    const ULONGLONG written = g_VsLoopbackWritten;
    if (Reader->Priming)
    {
        // Start (or restart after a gap) only once a full target's worth of new audio has arrived.
        if (written - Reader->PrimeFrom < VS_LOOPBACK_TARGET + frames)
        {
            KeReleaseSpinLock(&g_VsLoopbackLock, irql);
            return;
        }
        Reader->Position = written - VS_LOOPBACK_TARGET - frames;
        Reader->Priming = FALSE;
    }
    if (written < Reader->Position || written - Reader->Position > VS_LOOPBACK_MAX_DELAY + frames)
    {
        // Far behind (e.g. the capture stream was paused): jump back to the target distance.
        Reader->Position = written - VS_LOOPBACK_TARGET - frames;
    }
    ULONG available = (ULONG)(written - Reader->Position);
    ULONG count = min(frames, available);
    for (ULONG f = 0; f < count; ++f)
    {
        const SHORT s = g_VsLoopbackBuffer[(Reader->Position + f) % VS_LOOPBACK_SAMPLES];
        for (ULONG c = 0; c < channels; ++c)
        {
            out[f * channels + c] = s;
        }
    }
    Reader->Position += count;
    if (count < frames)
    {
        // Ran dry: nothing is being played (or the render side stalled). Silence, then prime again.
        Reader->Priming = TRUE;
        Reader->PrimeFrom = written;
    }
    KeReleaseSpinLock(&g_VsLoopbackLock, irql);
}
