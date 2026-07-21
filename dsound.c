#include <windows.h>
#include <dsound.h>
#include <stdio.h>

#ifdef NDEBUG
    #define printf(x)
#else
    #define printf(x) DebugPrint x

    void DebugPrint(const char *format, ...) {
        va_list args;
        char buffer[512];
        int i;
        va_start(args, format);
        i = _vsnprintf(buffer, sizeof(buffer), format, args);
        buffer[i < 0 ? sizeof(buffer) - 1 : i] = '\0';
        OutputDebugStringA(buffer);
        va_end(args);
    }
#endif

#if defined(_MSC_VER) && (_MSC_VER <= 1200)
    #ifndef LPCGUID
        #define LPCGUID LPGUID
    #endif
#endif

#define BUFFER_SIZE 65535
#define IS_8BIT(x) (x <= 8)

typedef struct {
    IDirectSoundBufferVtbl *lpVtbl;
    LONG ref;
    WAVEFORMATEX wfx;
    DWORD shrink, startTick, size;
    LPBYTE data;
    HANDLE hDevice;
} SB16DirectSoundBuffer;

HRESULT STDMETHODCALLTYPE DSB_QueryInterface(SB16DirectSoundBuffer *This, REFIID riid, LPVOID *ppv)
{
    printf(("DirectSoundBuffer::QueryInterface(This=0x%p, riid=0x%p, ppv=0x%p)\n", This, (LPVOID) riid, ppv));
    if (!IsEqualIID(riid, &IID_IDirectSoundBuffer) && !IsEqualIID(riid, &IID_IUnknown)) {
        *ppv = NULL;
        return DSERR_NOINTERFACE;
    }
    *ppv = This;
    This->ref ++;
    return DS_OK;
}

ULONG STDMETHODCALLTYPE DSB_AddRef(SB16DirectSoundBuffer *This)
{
    printf(("DirectSoundBuffer::AddRef(This=0x%p) -> before=%ld\n", This, This->ref));
    This->ref ++;
    return This->ref;
}

ULONG STDMETHODCALLTYPE DSB_Release(SB16DirectSoundBuffer *This)
{
    ULONG ref;
    printf(("DirectSoundBuffer::Release(This=0x%p) -> before=%ld\n", This, This->ref));
    This->ref --;
    ref = This->ref;
    if (ref == 0) {
        if (This->data != NULL) {
            HeapFree(GetProcessHeap(), 0, This->data);
            This->data = NULL;
        }
        HeapFree(GetProcessHeap(), 0, This);
    }
    return ref;
}

HRESULT STDMETHODCALLTYPE DSB_GetCaps(SB16DirectSoundBuffer *This, LPDSBCAPS caps)
{
    printf(("DirectSoundBuffer::GetCaps(This=0x%p, caps=0x%p)\n", This, caps));
    ZeroMemory(caps, sizeof(*caps));
    caps->dwSize = sizeof(*caps);
    return DS_OK;
}

DWORD GetPlayingPos(SB16DirectSoundBuffer *This)
{
    DWORD startTick, pos;
    startTick = This->startTick;
    if (startTick == 0) {
        printf(("DirectSoundBuffer::GetPlayingPos(This=0x%p) detected IDLE\n", This));
        return 0;
    }
    pos = (GetTickCount() - startTick)*11*This->shrink;
    if (pos < This->size) {
        printf(("DirectSoundBuffer::GetPlayingPos(This=0x%p) detected PLAYING\n", This));
        return pos == 0 ? 1 : pos;
    }
    This->startTick = 0;
    printf(("DirectSoundBuffer::GetPlayingPos(This=0x%p) detected STOPPED\n", This));
    return 0;
}

HRESULT STDMETHODCALLTYPE DSB_GetCurrentPosition(SB16DirectSoundBuffer *This, LPDWORD play, LPDWORD write)
{
    DWORD pos = GetPlayingPos(This);
    if (play != NULL) {
        *play = pos;
    }
    if (write != NULL) {
        *write = pos;
    }
    printf(("DirectSoundBuffer::GetCurrentPosition(This=0x%p) returns %lu, %lu", This, pos, pos));
    return DS_OK;
}

HRESULT STDMETHODCALLTYPE DSB_GetFormat(SB16DirectSoundBuffer *This, LPWAVEFORMATEX wfx, DWORD size, LPDWORD written)
{
    printf(("DirectSoundBuffer::GetFormat(This=0x%p, wfx=0x%p, size=%lu, written=0x%p)\n", This, wfx, size, written));
    if (size < sizeof(WAVEFORMATEX)) {
        return DSERR_INVALIDPARAM;
    }
    *written = sizeof(WAVEFORMATEX);
    memcpy(wfx, &This->wfx, sizeof(WAVEFORMATEX));
    return DS_OK;
}

HRESULT STDMETHODCALLTYPE DSB_GetVolume(SB16DirectSoundBuffer *This, LPLONG vol)
{
    printf(("DirectSoundBuffer::GetVolume(This=0x%p, vol=0x%p)\n", This, vol));
    *vol = 0;
    return DS_OK;
}

HRESULT STDMETHODCALLTYPE DSB_GetPan(SB16DirectSoundBuffer *This, LPLONG pan)
{
    printf(("DirectSoundBuffer::GetPan(This=0x%p, pan=0x%p)\n", This, pan));
    *pan = 0;
    return DS_OK;
}

HRESULT STDMETHODCALLTYPE DSB_GetFrequency(SB16DirectSoundBuffer *This, LPDWORD freq)
{
    printf(("DirectSoundBuffer::GetFrequency(This=0x%p, freq=0x%p)\n", This, freq));
    *freq = This->wfx.nSamplesPerSec;
    return DS_OK;
}

HRESULT STDMETHODCALLTYPE DSB_GetStatus(SB16DirectSoundBuffer *This, LPDWORD status)
{
    *status = GetPlayingPos(This) > 0 ? DSBSTATUS_PLAYING : 0;
    printf(("DirectSoundBuffer::GetStatus(This=0x%p) returns 0x%08lX\n", This, *status));
    return DS_OK;
}

HRESULT STDMETHODCALLTYPE DSB_Initialize(SB16DirectSoundBuffer *This, LPDIRECTSOUND ds, LPCDSBUFFERDESC desc)
{
    printf(("DirectSoundBuffer::Initialize(This=0x%p, ds=0x%p, desc=0x%p)\n", This, ds, desc));
    return DS_OK;
}

HRESULT STDMETHODCALLTYPE DSB_Lock(SB16DirectSoundBuffer *This, DWORD offset, DWORD bytes,
                                   LPVOID *p1, LPDWORD b1, LPVOID *p2, LPDWORD b2, DWORD flags)
{
    printf(("DirectSoundBuffer::Lock(This=0x%p, offset=%lu, bytes=%lu, p1=0x%p, b1=0x%p, p2=0x%p, b2=0x%p, flags=0x%08lX)\n",
           This, offset, bytes, p1, b1, p2, b2, flags));
    if (This->data == NULL || offset > This->size || bytes > This->size || offset + bytes > This->size) {
        return DSERR_INVALIDPARAM;
    }
    *p1 = This->data + offset;
    *b1 = bytes;
    if (p2 != NULL) {
        *p2 = NULL;
    }
    if (b2 != NULL) {
        *b2 = 0;
    }
    return DS_OK;
}

HRESULT STDMETHODCALLTYPE DSB_Play(SB16DirectSoundBuffer *This, DWORD reserved1, DWORD priority, DWORD flags)
{
    DWORD shrink, size, i, j, bytesWritten, startTick;
    LONG nSample;
    LPBYTE pBuf;
    BYTE data[BUFFER_SIZE];
    printf(("DirectSoundBuffer::Play(This=0x%p, reserved1=%lu, priority=%lu, flags=0x%08lX)\n", This, reserved1, priority, flags));
    if (This->data == NULL) {
        return DS_OK;
    }

    shrink = This->shrink;
    size = This->size/shrink;
    pBuf = This->data;
    if (size > BUFFER_SIZE) {
        size = BUFFER_SIZE;
    }
    if (IS_8BIT(This->wfx.wBitsPerSample)) {
        if (shrink == 1) {
            memcpy(data, pBuf, size);
        } else {
            for (i = 0; i < size; i ++) {
                nSample = 0;
                for (j = 0; j < shrink; j ++) {
                    nSample += *pBuf;
                    pBuf ++;
                }
                data[i] = (BYTE) (nSample/shrink);
            }
        }
    } else {
        shrink /= 2;
        pBuf ++;
        for (i = 0; i < size; i ++) {
            nSample = 0;
            for (j = 0; j < shrink; j ++) {
                nSample += (CHAR) (*pBuf);
                pBuf += 2;
            }
            data[i] = nSample/(LONG) shrink + 128;
        }
    }

    if (!WriteFile(This->hDevice, data, size, &bytesWritten, NULL)) {
        printf(("DirectSoundBuffer::Play can't write into DirectSB16: %lu\n", GetLastError()));
        return DSERR_GENERIC;
    }
    printf(("DirectSoundBuffer::Play wrote %lu bytes into DirectSB16\n", bytesWritten));
    startTick = GetTickCount();
    if (startTick == 0) {
        startTick = 1;
    }
    This->startTick = startTick;
    return DS_OK;
}

HRESULT STDMETHODCALLTYPE DSB_SetCurrentPosition(SB16DirectSoundBuffer *This, DWORD pos)
{
    printf(("DirectSoundBuffer::SetCurrentPosition(This=0x%p, pos=%lu)\n", This, pos));
    return DS_OK;
}

VOID CalcShrink(SB16DirectSoundBuffer *This)
{
    This->shrink = This->wfx.nChannels*((This->wfx.nSamplesPerSec + 8812)/11025);
    if (This->shrink == 0) {
        This->shrink = 1;
    }
    if (!IS_8BIT(This->wfx.wBitsPerSample)) {
        This->shrink *= 2;
    }
    printf(("DirectSoundBuffer::CalcShrink(This=0x%p) %u-channel, %luHz, %u-bit (%lux) -> Mono, 11025Hz, 8-bit\n",
            This, This->wfx.nChannels, This->wfx.nSamplesPerSec, This->wfx.wBitsPerSample, This->shrink));
}

HRESULT STDMETHODCALLTYPE DSB_SetFormat(SB16DirectSoundBuffer *This, LPCWAVEFORMATEX wfx)
{
    printf(("DirectSoundBuffer::SetFormat(This=0x%p, wfx=0x%p)\n", This, wfx));
    memcpy(&This->wfx, wfx, sizeof(WAVEFORMATEX));
    CalcShrink(This);
    return DS_OK;
}

HRESULT STDMETHODCALLTYPE DSB_SetVolume(SB16DirectSoundBuffer *This, LONG vol)
{
    printf(("DirectSoundBuffer::SetVolume(This=0x%p, vol=%ld)\n", This, vol));
    return DS_OK;
}

HRESULT STDMETHODCALLTYPE DSB_SetPan(SB16DirectSoundBuffer *This, LONG pan)
{
    printf(("DirectSoundBuffer::SetPan(This=0x%p, pan=%ld)\n", This, pan));
    return DS_OK;
}

HRESULT STDMETHODCALLTYPE DSB_SetFrequency(SB16DirectSoundBuffer *This, DWORD freq)
{
    printf(("DirectSoundBuffer::SetFrequency(This=0x%p, freq=%lu)\n", This, freq));
    This->wfx.nSamplesPerSec = (WORD) freq;
    CalcShrink(This);
    return DS_OK;
}

HRESULT STDMETHODCALLTYPE DSB_Stop(SB16DirectSoundBuffer *This)
{
    DWORD bytesWritten;
    BYTE byte = 128;
    printf(("DirectSoundBuffer::Stop(This=0x%p)\n", This));
    WriteFile(This->hDevice, &byte, 1, &bytesWritten, NULL);
    This->startTick = 0;
    return DS_OK;
}

HRESULT STDMETHODCALLTYPE DSB_Unlock(SB16DirectSoundBuffer *This, LPVOID p1, DWORD b1, LPVOID p2, DWORD b2)
{
    printf(("DirectSoundBuffer::Unlock(This=0x%p, p1=0x%p, b1=%lu, p2=0x%p, b2=%lu)\n", This, p1, b1, p2, b2));
    return DS_OK;
}

HRESULT STDMETHODCALLTYPE DSB_Restore(SB16DirectSoundBuffer *This)
{
    printf(("DirectSoundBuffer::Restore(This=0x%p)\n", This));
    return DS_OK;
}

IDirectSoundBufferVtbl SB16DSB_Vtbl = {
    (LPVOID) DSB_QueryInterface,
    (LPVOID) DSB_AddRef,
    (LPVOID) DSB_Release,
    (LPVOID) DSB_GetCaps,
    (LPVOID) DSB_GetCurrentPosition,
    (LPVOID) DSB_GetFormat,
    (LPVOID) DSB_GetVolume,
    (LPVOID) DSB_GetPan,
    (LPVOID) DSB_GetFrequency,
    (LPVOID) DSB_GetStatus,
    (LPVOID) DSB_Initialize,
    (LPVOID) DSB_Lock,
    (LPVOID) DSB_Play,
    (LPVOID) DSB_SetCurrentPosition,
    (LPVOID) DSB_SetFormat,
    (LPVOID) DSB_SetVolume,
    (LPVOID) DSB_SetPan,
    (LPVOID) DSB_SetFrequency,
    (LPVOID) DSB_Stop,
    (LPVOID) DSB_Unlock,
    (LPVOID) DSB_Restore
};

typedef struct {
    IDirectSoundVtbl *lpVtbl;
    LONG ref;
    HANDLE hDevice;
} SB16DirectSound;

HRESULT STDMETHODCALLTYPE DS_QueryInterface(SB16DirectSound *This, REFIID riid, LPVOID *ppv) {
    printf(("DirectSound::QueryInterface(This=0x%p, riid=0x%p, ppv=0x%p)\n", This, (LPVOID) riid, ppv));
    if (!IsEqualIID(riid, &IID_IDirectSound) && !IsEqualIID(riid, &IID_IUnknown)) {
        *ppv = NULL;
        return DSERR_NOINTERFACE;
    }
    *ppv = This;
    This->ref ++;
    return DS_OK;
}

ULONG STDMETHODCALLTYPE DS_AddRef(SB16DirectSound *This) {
    printf(("DirectSound::AddRef(This=0x%p) -> before=%ld\n", This, This->ref));
    This->ref ++;
    return This->ref;
}

ULONG STDMETHODCALLTYPE DS_Release(SB16DirectSound *This) {
    ULONG ref;
    printf(("DirectSound::Release(This=0x%p) -> before=%ld\n", This, This->ref));
    This->ref --;
    ref = This->ref;
    if (ref == 0) {
        CloseHandle(This->hDevice);
        HeapFree(GetProcessHeap(), 0, This);
    }
    return ref;
}

HRESULT STDMETHODCALLTYPE DS_CreateSoundBuffer(SB16DirectSound *This, LPCDSBUFFERDESC desc,
                                               LPDIRECTSOUNDBUFFER *ppBuf, LPUNKNOWN unk)
{
    SB16DirectSoundBuffer *buf;
    printf(("DirectSound::CreateSoundBuffer(This=0x%p, desc=0x%p, flags=0x%08lX, size=%lu, ppBuf=0x%p, unk=0x%p)\n",
            This, desc, desc->dwFlags, desc->dwBufferBytes, ppBuf, unk));

    buf = (SB16DirectSoundBuffer *) HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(SB16DirectSoundBuffer));
    if (buf == NULL) {
        printf(("DirectSound::CreateSoundBuffer can't allocate IDirectSoundBuffer: %lu\n", GetLastError()));
        return DSERR_OUTOFMEMORY;
    }
    buf->lpVtbl = &SB16DSB_Vtbl;
    buf->ref = 1;

    if (desc->lpwfxFormat == NULL) {
        buf->wfx.wFormatTag = WAVE_FORMAT_PCM;
        buf->wfx.nChannels = 1;
        buf->wfx.nSamplesPerSec = 11025;
        buf->wfx.nAvgBytesPerSec = 11025;
        buf->wfx.nBlockAlign = 1;
        buf->wfx.wBitsPerSample = 8;
        buf->wfx.cbSize = sizeof(WAVEFORMATEX);
    } else {
        memcpy(&buf->wfx, desc->lpwfxFormat, sizeof(WAVEFORMATEX));
    }
    CalcShrink(buf);

    buf->startTick = 0;
    buf->size = desc->dwBufferBytes;
    if (buf->size == 0) {
        buf->data = NULL;
    } else {
        buf->data = (LPBYTE) HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, buf->size);
        if (buf->data == NULL) {
            printf(("DirectSound::CreateSoundBuffer can't allocate %lu (dwBufferBytes) bytes: %lu\n", buf->size, GetLastError()));
            HeapFree(GetProcessHeap(), 0, buf);
            return DSERR_OUTOFMEMORY;
        }
        if (IS_8BIT(buf->wfx.wBitsPerSample)) {
            memset(buf->data, 128, buf->size);
        }
    }
    buf->hDevice = This->hDevice;

    *ppBuf = (LPDIRECTSOUNDBUFFER) buf;
    printf(("DirectSound::CreateSoundBuffer done: 0x%p\n", buf));
    return DS_OK;
}

HRESULT STDMETHODCALLTYPE DS_GetCaps(SB16DirectSound *This, LPDSCAPS caps)
{
    printf(("DirectSound::GetCaps(This=0x%p, caps=0x%p)\n", This, caps));
    ZeroMemory(caps, sizeof(*caps));
    caps->dwSize = sizeof(*caps);
    caps->dwFlags = DSCAPS_PRIMARYMONO | DSCAPS_PRIMARYSTEREO | DSCAPS_PRIMARY8BIT | DSCAPS_PRIMARY16BIT | DSCAPS_CERTIFIED |
                    DSCAPS_SECONDARYMONO | DSCAPS_SECONDARYSTEREO | DSCAPS_SECONDARY8BIT | DSCAPS_SECONDARY16BIT;
    caps->dwMinSecondarySampleRate = 4000;
    caps->dwMaxSecondarySampleRate = 48000;
    caps->dwPrimaryBuffers = 1;
    return DS_OK;
}

HRESULT STDMETHODCALLTYPE DS_DuplicateSoundBuffer(SB16DirectSound *This, LPDIRECTSOUNDBUFFER pOriginal, LPDIRECTSOUNDBUFFER *ppDuplicate)
{
    SB16DirectSoundBuffer *orig, *dup;
    printf(("DirectSound::DuplicateSoundBuffer(This=0x%p, pOriginal=0x%p, ppDuplicate=0x%p)\n", This, pOriginal, ppDuplicate));

    orig = (SB16DirectSoundBuffer *) pOriginal;
    dup = (SB16DirectSoundBuffer *) HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(SB16DirectSoundBuffer));
    if (dup == NULL) {
        printf(("DirectSound::DuplicateSoundBuffer can't allocate IDirectSoundBuffer: %lu\n", GetLastError()));
        return DSERR_OUTOFMEMORY;
    }
    dup->lpVtbl = pOriginal->lpVtbl;
    dup->ref = 1;

    memcpy(&dup->wfx, &orig->wfx, sizeof(WAVEFORMATEX));
    dup->shrink = orig->shrink;

    dup->startTick = 0;
    dup->size = orig->size;
    if (dup->size == 0) {
        dup->data = NULL;
    } else {
        // TODO dup->data = orig->data, AddRef (implement a global ref for data)
        dup->data = (LPBYTE) HeapAlloc(GetProcessHeap(), 0, dup->size);
        if (dup->data == NULL) {
            printf(("DirectSound::DuplicateSoundBuffer can't allocate %lu (pOriginal->dwBufferBytes) bytes: %lu\n", dup->size, GetLastError()));
            HeapFree(GetProcessHeap(), 0, dup);
            return DSERR_OUTOFMEMORY;
        }
        memcpy(dup->data, orig->data, dup->size);
    }
    dup->hDevice = This->hDevice;

    *ppDuplicate = (LPDIRECTSOUNDBUFFER) dup;
    printf(("DirectSound::DuplicateSoundBuffer done: 0x%p\n", dup));
    return DS_OK;
}

HRESULT STDMETHODCALLTYPE DS_SetCooperativeLevel(SB16DirectSound *This, HWND hwnd, DWORD level) {
    printf(("DirectSound::SetCooperativeLevel(This=0x%p, hwnd=0x%p, level=%lu)\n", This, hwnd, level));
    return DS_OK;
}

HRESULT STDMETHODCALLTYPE DS_Compact(SB16DirectSound *This)
{
    printf(("DirectSound::Compact(This=0x%p)\n", This));
    return DS_OK;
}

HRESULT STDMETHODCALLTYPE DS_GetSpeakerConfig(SB16DirectSound *This, LPDWORD pdwConfig)
{
    printf(("DirectSound::GetSpeakerConfig(This=0x%p, pdwConfig=0x%p)\n", This, pdwConfig));
    *pdwConfig = DSSPEAKER_STEREO;
    return DS_OK;
}

HRESULT STDMETHODCALLTYPE DS_SetSpeakerConfig(SB16DirectSound *This, DWORD config)
{
    printf(("DirectSound::SetSpeakerConfig(This=0x%p, dwConfig=0x%08lX)\n", This, config));
    return DS_OK;
}

HRESULT STDMETHODCALLTYPE DS_Initialize(SB16DirectSound *This, LPGUID lpGuid)
{
    printf(("DirectSound::Initialize(This=0x%p, lpGuid=0x%p)\n", This, lpGuid));
    return DS_OK;
}

IDirectSoundVtbl SB16DS_Vtbl = {
    (LPVOID) DS_QueryInterface,
    (LPVOID) DS_AddRef,
    (LPVOID) DS_Release,
    (LPVOID) DS_CreateSoundBuffer,
    (LPVOID) DS_GetCaps,
    (LPVOID) DS_DuplicateSoundBuffer,
    (LPVOID) DS_SetCooperativeLevel,
    (LPVOID) DS_Compact,
    (LPVOID) DS_GetSpeakerConfig,
    (LPVOID) DS_SetSpeakerConfig,
    (LPVOID) DS_Initialize
};

HRESULT WINAPI DirectSoundCreate(LPCGUID lpGuid, LPDIRECTSOUND *ppDS, LPUNKNOWN pUnk)
{
    HANDLE hDevice;
    SB16DirectSound *obj;
    printf(("DirectSoundCreate(lpGuid=0x%p, ppDS=0x%p, pUnk=0x%p)\n", lpGuid, ppDS, pUnk));

    hDevice = CreateFile("\\\\.\\DirectSB16", GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (hDevice == INVALID_HANDLE_VALUE) {
        printf(("DirectSoundCreate can't open DirectSB16 driver: %lu\n", GetLastError()));
        return DSERR_NODRIVER;
    }

    obj = (SB16DirectSound *) HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(SB16DirectSound));
    if (obj == NULL) {
        printf(("DirectSoundCreate can't allocate IDirectSound: %lu\n", GetLastError()));
        CloseHandle(hDevice);
        return DSERR_OUTOFMEMORY;
    }
    obj->lpVtbl = &SB16DS_Vtbl;
    obj->ref = 1;
    obj->hDevice = hDevice;

    *ppDS = (LPDIRECTSOUND) obj;
    printf(("DirectSoundCreate done: 0x%p\n", obj));
    return DS_OK;
}

BOOL WINAPI DllMain(HANDLE hDll, DWORD dwReason, LPVOID lpReserved)
{
    if (dwReason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hDll);
    }
    return TRUE;
}
