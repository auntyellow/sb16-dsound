#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <io.h>
#include <dsound.h>

#if defined(_MSC_VER) && (_MSC_VER <= 1200)
    #ifndef LPCGUID
        #define LPCGUID LPGUID
    #endif
#endif

#define GUID_STRLEN 40
#define DWORD_STRLEN 12
#define LP_STRLEN 12

#ifdef NDEBUG

#define printf(x)
#define dump_guid(szGuid, lpGuid) (void) (szGuid)
#define dump_dword(szdw, lpdw) (void) (szdw)
#define dump_lp(szdw, lplp) (void) (szdw)

#else

#define printf(x) DebugPrint x

static FILE *g_fpDebug = NULL;
static int g_fdDebug = -1;

void DebugPrint(const char *format, ...)
{
    va_list args;
    char buffer[1024];
    int i;
    va_start(args, format);
    if (g_fpDebug) {
        vfprintf(g_fpDebug, format, args);
        fflush(g_fpDebug);
        if (g_fdDebug != -1) {
            _commit(g_fdDebug);
        }
    } else {
        i = _vsnprintf(buffer, sizeof(buffer), format, args);
        buffer[i < 0 ? sizeof(buffer) - 1 : i] = '\0';
        OutputDebugStringA(buffer);
    }
    va_end(args);
}

void dump_guid(LPSTR szGuid, LPCGUID lpGuid) {
    if (lpGuid) {
        sprintf(szGuid, "{%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
                lpGuid->Data1, lpGuid->Data2, lpGuid->Data3, lpGuid->Data4[0], lpGuid->Data4[1],
                lpGuid->Data4[2], lpGuid->Data4[3], lpGuid->Data4[4], lpGuid->Data4[5], lpGuid->Data4[6], lpGuid->Data4[7]);
    } else {
        strcpy(szGuid, "NULL");
    }
}

void dump_dword(LPSTR szdw, LPDWORD lpdw) {
    if (lpdw) {
        sprintf(szdw, "%lu", *lpdw);
    } else {
        strcpy(szdw, "NULL");
    }
}

void dump_lp(LPSTR szdw, LPVOID *lplp) {
    if (lplp) {
        sprintf(szdw, "0x%p", *lplp);
    } else {
        strcpy(szdw, "NULL");
    }
}

#endif

static HMODULE g_hRealDSound = NULL;

typedef HRESULT (WINAPI *DirectSoundCreate_t)(LPCGUID, LPDIRECTSOUND *, LPUNKNOWN);
static DirectSoundCreate_t pReal_DirectSoundCreate = NULL;

static void LoadRealDSound()
{
    char sysPath[MAX_PATH];
    if (g_hRealDSound) {
        printf(("LoadRealDSound: ddraw.dll already loaded\n"));
        return;
    }

    if (!GetSystemDirectoryA(sysPath, MAX_PATH)) {
        printf(("LoadRealDSound: GetSystemDirectoryA failed\n"));
        return;
    }
    strcat(sysPath, "\\dsound.dll");

    g_hRealDSound = LoadLibraryA(sysPath);
    if (!g_hRealDSound) {
        printf(("LoadRealDSound: LoadLibraryA failed, error = %lu, path = %s\n", GetLastError(), sysPath));
        return;
    }

    pReal_DirectSoundCreate = (DirectSoundCreate_t) GetProcAddress(g_hRealDSound, "DirectSoundCreate");
    printf(("LoadRealDSound: GetProcAddress(DirectSoundCreate) = %p\n", pReal_DirectSoundCreate));
}

typedef struct WrappedDirectSound {
    IDirectSoundVtbl *lpVtbl;
    LPDIRECTSOUND real;
    LONG ref;
} WrappedDirectSound;

typedef struct WrappedBuffer {
    IDirectSoundBufferVtbl *lpVtbl;
    LPDIRECTSOUNDBUFFER real;
    LONG ref;
} WrappedBuffer;

WrappedBuffer *WRAP_Buffer(LPDIRECTSOUNDBUFFER real);

ULONG STDMETHODCALLTYPE DS_AddRef(WrappedDirectSound *This)
{
    LONG newRealRef = This->real->lpVtbl->AddRef(This->real);
    LONG newWrappedRef = InterlockedIncrement(&This->ref);
    printf(("DS_AddRef: this=0x%p new_wrapped_ref=%ld new_real_ref=%ld\n", This, newWrappedRef, newRealRef));
    UNREFERENCED_PARAMETER(newRealRef);
    return newWrappedRef;
}

HRESULT STDMETHODCALLTYPE DS_QueryInterface(WrappedDirectSound *This, REFIID riid, LPVOID *ppvObj)
{
    char szGuid[GUID_STRLEN];
    dump_guid(szGuid, (LPGUID) riid);
    printf(("DS_QueryInterface: this=0x%p riid=%s out=0x%p\n", This, szGuid, ppvObj));

    if (!ppvObj) {
        return E_POINTER;
    }
    *ppvObj = NULL;

    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IDirectSound)) {
        DS_AddRef(This);
        *ppvObj = This;
        return S_OK;
    }

    return This->real->lpVtbl->QueryInterface(This->real, riid, ppvObj);
}

ULONG STDMETHODCALLTYPE DS_Release(WrappedDirectSound *This)
{
    LONG newRealRef = This->real->lpVtbl->Release(This->real);
    LONG newWrappedRef = InterlockedDecrement(&This->ref);
    printf(("DS_Release: this=0x%p new_wrapped_ref=%ld new_real_ref=%ld\n", This, newWrappedRef, newRealRef));
    UNREFERENCED_PARAMETER(newRealRef);
    if (newWrappedRef == 0) {
        HeapFree(GetProcessHeap(), 0, This->lpVtbl);
        HeapFree(GetProcessHeap(), 0, This);
        return 0;
    }
    return newWrappedRef;
}

HRESULT STDMETHODCALLTYPE DS_CreateSoundBuffer(WrappedDirectSound *This, LPCDSBUFFERDESC lpcdsbdesc, WrappedBuffer **ppBuffer, LPUNKNOWN lpu)
{
    LPDIRECTSOUNDBUFFER real;
    HRESULT hr;
    printf(("DS_CreateSoundBuffer: this=0x%p desc=0x%p out_wrapped=0x%p\n", This, lpcdsbdesc, ppBuffer));

    if (!ppBuffer) {
        return DSERR_INVALIDPARAM;
    }
    real = NULL;
    hr = This->real->lpVtbl->CreateSoundBuffer(This->real, lpcdsbdesc, &real, lpu);
    if (FAILED(hr)) {
        printf(("DS_CreateSoundBuffer returned 0x%08lX\n", hr));
        return hr;
    }
    *ppBuffer = WRAP_Buffer(real);
    printf(("DS_CreateSoundBuffer: returned wrapped=0x%p\n", *ppBuffer));
    return S_OK;
}

HRESULT STDMETHODCALLTYPE DS_GetCaps(WrappedDirectSound *This, LPDSCAPS lpdscaps)
{
    HRESULT hr = This->real->lpVtbl->GetCaps(This->real, lpdscaps);
    printf(("DS_GetCaps: this=0x%p size=%lu flags=0x%08lX primary_buffers=%lu total_mem=%lu free_mem=%lu result=0x%08lX\n",
            This, lpdscaps->dwSize, lpdscaps->dwFlags, lpdscaps->dwPrimaryBuffers, lpdscaps->dwTotalHwMemBytes, lpdscaps->dwFreeHwMemBytes, hr));
    return hr;
}

HRESULT STDMETHODCALLTYPE DS_DuplicateSoundBuffer(WrappedDirectSound *This, WrappedBuffer *src, WrappedBuffer **ppBuffer)
{
    LPDIRECTSOUNDBUFFER real;
    HRESULT hr;
    printf(("DS_DuplicateSoundBuffer: this=0x%p src_wrapped=%p out_wrapped=%p\n", This, src, ppBuffer));
    if (!src || !ppBuffer) {
        return DSERR_INVALIDPARAM;
    }
    real = NULL;
    hr = This->real->lpVtbl->DuplicateSoundBuffer(This->real, src->real, &real);
    if (FAILED(hr)) {
        printf(("DS_DuplicateSoundBuffer returned 0x%08lX\n", hr));
        return hr;
    }
    *ppBuffer = WRAP_Buffer(real);
    printf(("DS_DuplicateSoundBuffer: returned wrapped=0x%p\n", *ppBuffer));
    return S_OK;
}

HRESULT STDMETHODCALLTYPE DS_SetCooperativeLevel(WrappedDirectSound *This, HWND hwnd, DWORD dwLevel)
{
    HRESULT hr = This->real->lpVtbl->SetCooperativeLevel(This->real, hwnd, dwLevel);
    printf(("DS_SetCooperativeLevel: this=0x%p hwnd=0x%p level=%lu result=0x%08lX\n", This, hwnd, dwLevel, hr));
    return hr;
}

HRESULT STDMETHODCALLTYPE DS_Compact(WrappedDirectSound *This)
{
    HRESULT hr = This->real->lpVtbl->Compact(This->real);
    printf(("DS_Compact: this=0x%p result=0x%08lX\n", This, hr));
    return hr;
}

HRESULT STDMETHODCALLTYPE DS_GetSpeakerConfig(WrappedDirectSound *This, LPDWORD lpdwSpeakerConfig)
{
    HRESULT hr = This->real->lpVtbl->GetSpeakerConfig(This->real, lpdwSpeakerConfig);
    printf(("DS_GetSpeakerConfig: this=0x%p config=0x%08lX result=0x%08lX\n", This, *lpdwSpeakerConfig, hr));
    return hr;
}

HRESULT STDMETHODCALLTYPE DS_SetSpeakerConfig(WrappedDirectSound *This, DWORD dwSpeakerConfig)
{
    HRESULT hr = This->real->lpVtbl->SetSpeakerConfig(This->real, dwSpeakerConfig);
    printf(("DS_SetSpeakerConfig: this=0x%p config=0x%08lX result=0x%08lX\n", This, dwSpeakerConfig, hr));
    return hr;
}

HRESULT STDMETHODCALLTYPE DS_Initialize(WrappedDirectSound *This, LPCGUID lpcGuid)
{
    char szGuid[GUID_STRLEN];
    HRESULT hr = This->real->lpVtbl->Initialize(This->real, lpcGuid);
    dump_guid(szGuid, lpcGuid);
    printf(("DS_Initialize: this=0x%p guid=%s result=0x%08lX\n", This, szGuid, hr));
    return hr;
}

WrappedDirectSound *WRAP_DirectSound(LPDIRECTSOUND real)
{
    WrappedDirectSound *w;
    IDirectSoundVtbl *lpVtbl;
    if (!real) {
        return NULL;
    }

    w = (WrappedDirectSound *) HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(WrappedDirectSound));
    if (!w) {
        return NULL;
    }

    lpVtbl = (IDirectSoundVtbl *) HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(IDirectSoundVtbl));
    if (!lpVtbl) {
        HeapFree(GetProcessHeap(), 0, w);
        return NULL;
    }

    lpVtbl->QueryInterface = (HRESULT (STDMETHODCALLTYPE *)(LPDIRECTSOUND, REFIID, LPVOID *)) DS_QueryInterface;
    lpVtbl->AddRef = (ULONG (STDMETHODCALLTYPE *)(LPDIRECTSOUND)) DS_AddRef;
    lpVtbl->Release = (ULONG (STDMETHODCALLTYPE *)(LPDIRECTSOUND)) DS_Release;

    lpVtbl->CreateSoundBuffer = (HRESULT (STDMETHODCALLTYPE *)(LPDIRECTSOUND, LPCDSBUFFERDESC, LPDIRECTSOUNDBUFFER *, LPUNKNOWN)) DS_CreateSoundBuffer;
    lpVtbl->GetCaps = (HRESULT (STDMETHODCALLTYPE *)(LPDIRECTSOUND, LPDSCAPS)) DS_GetCaps;
    lpVtbl->DuplicateSoundBuffer = (HRESULT (STDMETHODCALLTYPE *)(LPDIRECTSOUND, LPDIRECTSOUNDBUFFER, LPDIRECTSOUNDBUFFER *)) DS_DuplicateSoundBuffer;
    lpVtbl->SetCooperativeLevel = (HRESULT (STDMETHODCALLTYPE *)(LPDIRECTSOUND, HWND, DWORD)) DS_SetCooperativeLevel;
    lpVtbl->Compact = (HRESULT (STDMETHODCALLTYPE *)(LPDIRECTSOUND)) DS_Compact;
    lpVtbl->GetSpeakerConfig = (HRESULT (STDMETHODCALLTYPE *)(LPDIRECTSOUND, LPDWORD)) DS_GetSpeakerConfig;
    lpVtbl->SetSpeakerConfig = (HRESULT (STDMETHODCALLTYPE *)(LPDIRECTSOUND, DWORD)) DS_SetSpeakerConfig;
    lpVtbl->Initialize = (HRESULT (STDMETHODCALLTYPE *)(LPDIRECTSOUND, LPCGUID)) DS_Initialize;

    w->lpVtbl = lpVtbl;
    w->real = real;
    w->ref = 1;
    return w;
}

ULONG STDMETHODCALLTYPE DSB_AddRef(WrappedBuffer *This)
{
    LONG newRealRef = This->real->lpVtbl->AddRef(This->real);
    LONG newWrappedRef = InterlockedIncrement(&This->ref);
    printf(("DSB_AddRef: this=0x%p new_wrapped_ref=%ld new_real_ref=%ld\n", This, newWrappedRef, newRealRef));
    UNREFERENCED_PARAMETER(newRealRef);
    return newWrappedRef;
}

HRESULT STDMETHODCALLTYPE DSB_QueryInterface(WrappedBuffer *This, REFIID riid, LPVOID *ppvObj)
{
    char szGuid[GUID_STRLEN];
    dump_guid(szGuid, (LPGUID) riid);
    printf(("DSB_QueryInterface: this=0x%p riid=%s out=0x%p\n", This, szGuid, ppvObj));

    if (!ppvObj) {
        return E_POINTER;
    }
    *ppvObj = NULL;

    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IDirectSoundBuffer)) {
        DSB_AddRef(This);
        *ppvObj = This;
        return S_OK;
    }
    return This->real->lpVtbl->QueryInterface(This->real, riid, ppvObj);
}

ULONG STDMETHODCALLTYPE DSB_Release(WrappedBuffer *This)
{
    LONG newRealRef = This->real->lpVtbl->Release(This->real);
    LONG newWrappedRef = InterlockedDecrement(&This->ref);
    printf(("DSB_Release: this=0x%p new_wrapped_ref=%ld new_real_ref=%ld\n", This, newWrappedRef, newRealRef));
    UNREFERENCED_PARAMETER(newRealRef);
    if (newWrappedRef == 0) {
        HeapFree(GetProcessHeap(), 0, This->lpVtbl);
        HeapFree(GetProcessHeap(), 0, This);
        return 0;
    }
    return newWrappedRef;
}

HRESULT STDMETHODCALLTYPE DSB_GetCaps(WrappedBuffer *This, LPDSBCAPS lpdsbcaps)
{
    HRESULT hr = This->real->lpVtbl->GetCaps(This->real, lpdsbcaps);
    printf(("DSB_GetCaps: this=0x%p size=%lu flags=0x%08lX buffer_bytes=%lu unlock_transfer_rate=%lu play_cpu_overhead=%lu result=0x%08lX\n",
            This, lpdsbcaps->dwSize, lpdsbcaps->dwFlags, lpdsbcaps->dwBufferBytes, lpdsbcaps->dwUnlockTransferRate, lpdsbcaps->dwPlayCpuOverhead, hr));
    return hr;
}

HRESULT STDMETHODCALLTYPE DSB_GetCurrentPosition(WrappedBuffer *This, LPDWORD lpdwCurrentPlayCursor, LPDWORD lpdwCurrentWriteCursor)
{
    char szPlay[DWORD_STRLEN], szWrite[DWORD_STRLEN];
    HRESULT hr = This->real->lpVtbl->GetCurrentPosition(This->real, lpdwCurrentPlayCursor, lpdwCurrentWriteCursor);
    dump_dword(szPlay, lpdwCurrentPlayCursor);
    dump_dword(szWrite, lpdwCurrentWriteCursor);
    printf(("DSB_GetCurrentPosition: this=0x%p play_cursor=%s, write_cursor=%s result=0x%08lX\n", This, szPlay, szWrite, hr));
    return hr;
}

HRESULT STDMETHODCALLTYPE DSB_GetFormat(WrappedBuffer *This, LPWAVEFORMATEX lpwf, DWORD dwSizeAllocated, LPDWORD lpdwSizeWritten)
{
    char szSizeWritten[DWORD_STRLEN];
    HRESULT hr = This->real->lpVtbl->GetFormat(This->real, lpwf, dwSizeAllocated, lpdwSizeWritten);
    dump_dword(szSizeWritten, lpdwSizeWritten);
    printf(("DSB_GetFormat: this=0x%p format=0x%p size_allocated=%lu size_written=%s result=0x%08lX\n", This, lpwf, dwSizeAllocated, szSizeWritten, hr));
    return hr;
}

HRESULT STDMETHODCALLTYPE DSB_GetVolume(WrappedBuffer *This, LPLONG lplVolume)
{
    HRESULT hr = This->real->lpVtbl->GetVolume(This->real, lplVolume);
    printf(("DSB_GetVolume: this=0x%p volume=%lu result=0x%08lX\n", This, *lplVolume, hr));
    return hr;
}

HRESULT STDMETHODCALLTYPE DSB_GetPan(WrappedBuffer *This, LPLONG lplPan)
{
    HRESULT hr = This->real->lpVtbl->GetPan(This->real, lplPan);
    printf(("DSB_GetPan: this=0x%p pan=%lu result=0x%08lX\n", This, *lplPan, hr));
    return hr;
}

HRESULT STDMETHODCALLTYPE DSB_GetFrequency(WrappedBuffer *This, LPDWORD lpdwFrequency)
{
    HRESULT hr = This->real->lpVtbl->GetFrequency(This->real, lpdwFrequency);
    printf(("DSB_GetFrequency: this=0x%p frequency=%lu result=0x%08lX\n", This, *lpdwFrequency, hr));
    return hr;
}

HRESULT STDMETHODCALLTYPE DSB_GetStatus(WrappedBuffer *This, LPDWORD lpdwStatus)
{
    HRESULT hr = This->real->lpVtbl->GetStatus(This->real, lpdwStatus);
    printf(("DSB_GetStatus: this=0x%p status=0x%08lX result=0x%08lX\n", This, *lpdwStatus, hr));
    return hr;
}

HRESULT STDMETHODCALLTYPE DSB_Initialize(WrappedBuffer *This, WrappedDirectSound *lpDS, LPCDSBUFFERDESC lpcdsbd)
{
    HRESULT hr = This->real->lpVtbl->Initialize(This->real, lpDS->real, lpcdsbd);
    printf(("DSB_Initialize: this=0x%p ds_wrapped=0x%p desc=0x%p result=0x%08lX\n", This, lpDS, lpcdsbd, hr));
    return hr;
}

HRESULT STDMETHODCALLTYPE DSB_Lock(WrappedBuffer *This, DWORD dwOffset, DWORD dwBytes,
        LPVOID *lplpvAudioPtr1, LPDWORD lpdwAudioBytes1, LPVOID *lplpvAudioPtr2, LPDWORD lpdwAudioBytes2, DWORD dwFlags)
{
    char szlp1[LP_STRLEN], szlp2[LP_STRLEN];
    char szBytes1[LP_STRLEN], szBytes2[DWORD_STRLEN];
    HRESULT hr = This->real->lpVtbl->Lock(This->real, dwOffset, dwBytes, lplpvAudioPtr1, lpdwAudioBytes1, lplpvAudioPtr2, lpdwAudioBytes2, dwFlags);
    dump_lp(szlp1, lplpvAudioPtr1);
    dump_lp(szlp2, lplpvAudioPtr2);
    dump_dword(szBytes1, lpdwAudioBytes1);
    dump_dword(szBytes2, lpdwAudioBytes2);
    printf(("DSB_Lock: this=0x%p offset=%lu bytes=%lu ptr1=%s bytes1=%s ptr2=%s bytes2=%s flags=0x%08lX result=0x%08lX\n",
            This, dwOffset, dwBytes, szlp1, szBytes1, szlp2, szBytes2, dwFlags, hr));
    return hr;
}

HRESULT STDMETHODCALLTYPE DSB_Play(WrappedBuffer *This, DWORD dwReserved1, DWORD dwPriority, DWORD dwFlags)
{
    HRESULT hr = This->real->lpVtbl->Play(This->real, dwReserved1, dwPriority, dwFlags);
    printf(("DSB_Play: this=0x%p reserved=%lu priority=%lu flags=0x%08lX result=0x%08lX\n", This, dwReserved1, dwPriority, dwFlags, hr));
    return hr;
}

HRESULT STDMETHODCALLTYPE DSB_SetCurrentPosition(WrappedBuffer *This, DWORD dwNewPosition)
{
    HRESULT hr = This->real->lpVtbl->SetCurrentPosition(This->real, dwNewPosition);
    printf(("DSB_SetCurrentPosition: this=0x%p pos=%lu result=0x%08lX\n", This, dwNewPosition, hr));
    return hr;
}

HRESULT STDMETHODCALLTYPE DSB_SetFormat(WrappedBuffer *This, LPCWAVEFORMATEX lpcwf)
{
    HRESULT hr = This->real->lpVtbl->SetFormat(This->real, lpcwf);
    printf(("DSB_SetFormat: this=0x%p format=0x%p result=0x%08lX\n", This, lpcwf, hr));
    return hr;
}

HRESULT STDMETHODCALLTYPE DSB_SetVolume(WrappedBuffer *This, LONG lVolume)
{
    HRESULT hr = This->real->lpVtbl->SetVolume(This->real, lVolume);
    printf(("DSB_SetVolume: this=0x%p volume=%ld result=0x%08lX\n", This, lVolume, hr));
    return hr;
}

HRESULT STDMETHODCALLTYPE DSB_SetPan(WrappedBuffer *This, LONG lPan)
{
    HRESULT hr = This->real->lpVtbl->SetPan(This->real, lPan);
    printf(("DSB_SetPan: this=0x%p pan=%ld result=0x%08lX\n", This, lPan, hr));
    return hr;
}

HRESULT STDMETHODCALLTYPE DSB_SetFrequency(WrappedBuffer *This, DWORD dwFrequency)
{
    HRESULT hr = This->real->lpVtbl->SetFrequency(This->real, dwFrequency);
    printf(("DSB_SetFrequency: this=0x%p frequency=%lu result=0x%08lX\n", This, dwFrequency, hr));
    return hr;
}

HRESULT STDMETHODCALLTYPE DSB_Stop(WrappedBuffer *This)
{
    HRESULT hr = This->real->lpVtbl->Stop(This->real);
    printf(("DSB_Stop: this=0x%p result=0x%08lX\n", This, hr));
    return hr;
}

HRESULT STDMETHODCALLTYPE DSB_Unlock(WrappedBuffer *This, LPVOID lpvAudioPtr1, DWORD dwAudioBytes1, LPVOID lpvAudioPtr2, DWORD dwAudioBytes2)
{
    HRESULT hr = This->real->lpVtbl->Unlock(This->real, lpvAudioPtr1, dwAudioBytes1, lpvAudioPtr2, dwAudioBytes2);
    printf(("DSB_Unlock: this=0x%p ptr1=0x%p bytes1=%lu ptr2=0x%p bytes2=%lu result=0x%08lX\n",
            This, lpvAudioPtr1, dwAudioBytes1, lpvAudioPtr2, dwAudioBytes2, hr));
    return hr;
}

HRESULT STDMETHODCALLTYPE DSB_Restore(WrappedBuffer *This)
{
    HRESULT hr = This->real->lpVtbl->Restore(This->real);
    printf(("DSB_Restore: this=0x%p result=0x%08lX\n", This, hr));
    return hr;
}

WrappedBuffer *WRAP_Buffer(LPDIRECTSOUNDBUFFER real)
{
    WrappedBuffer *w;
    IDirectSoundBufferVtbl *lpVtbl;
    if (!real) {
        return NULL;
    }

    w = (WrappedBuffer *) HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(WrappedBuffer));
    if (!w) {
        return NULL;
    }

    lpVtbl = (IDirectSoundBufferVtbl *) HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(IDirectSoundBufferVtbl));
    if (!lpVtbl) {
        HeapFree(GetProcessHeap(), 0, w);
        return NULL;
    }

    lpVtbl->QueryInterface = (HRESULT (STDMETHODCALLTYPE *)(LPDIRECTSOUNDBUFFER, REFIID, LPVOID *)) DSB_QueryInterface;
    lpVtbl->AddRef = (ULONG (STDMETHODCALLTYPE *)(LPDIRECTSOUNDBUFFER)) DSB_AddRef;
    lpVtbl->Release = (ULONG (STDMETHODCALLTYPE *)(LPDIRECTSOUNDBUFFER)) DSB_Release;

    lpVtbl->GetCaps = (HRESULT (STDMETHODCALLTYPE *)(LPDIRECTSOUNDBUFFER, LPDSBCAPS)) DSB_GetCaps;
    lpVtbl->GetCurrentPosition = (HRESULT (STDMETHODCALLTYPE *)(LPDIRECTSOUNDBUFFER, LPDWORD, LPDWORD)) DSB_GetCurrentPosition;
    lpVtbl->GetFormat = (HRESULT (STDMETHODCALLTYPE *)(LPDIRECTSOUNDBUFFER, LPWAVEFORMATEX, DWORD, LPDWORD)) DSB_GetFormat;
    lpVtbl->GetVolume = (HRESULT (STDMETHODCALLTYPE *)(LPDIRECTSOUNDBUFFER, LPLONG)) DSB_GetVolume;
    lpVtbl->GetPan = (HRESULT (STDMETHODCALLTYPE *)(LPDIRECTSOUNDBUFFER, LPLONG)) DSB_GetPan;
    lpVtbl->GetFrequency = (HRESULT (STDMETHODCALLTYPE *)(LPDIRECTSOUNDBUFFER, LPDWORD)) DSB_GetFrequency;
    lpVtbl->GetStatus = (HRESULT (STDMETHODCALLTYPE *)(LPDIRECTSOUNDBUFFER, LPDWORD)) DSB_GetStatus;
    lpVtbl->Initialize = (HRESULT (STDMETHODCALLTYPE *)(LPDIRECTSOUNDBUFFER, LPDIRECTSOUND, LPCDSBUFFERDESC)) DSB_Initialize;
    lpVtbl->Lock = (HRESULT (STDMETHODCALLTYPE *)(LPDIRECTSOUNDBUFFER, DWORD, DWORD, LPVOID *, LPDWORD, LPVOID *, LPDWORD, DWORD)) DSB_Lock;
    lpVtbl->Play = (HRESULT (STDMETHODCALLTYPE *)(LPDIRECTSOUNDBUFFER, DWORD, DWORD, DWORD)) DSB_Play;
    lpVtbl->SetCurrentPosition = (HRESULT (STDMETHODCALLTYPE *)(LPDIRECTSOUNDBUFFER, DWORD)) DSB_SetCurrentPosition;
    lpVtbl->SetFormat = (HRESULT (STDMETHODCALLTYPE *)(LPDIRECTSOUNDBUFFER, LPCWAVEFORMATEX)) DSB_SetFormat;
    lpVtbl->SetVolume = (HRESULT (STDMETHODCALLTYPE *)(LPDIRECTSOUNDBUFFER, LONG)) DSB_SetVolume;
    lpVtbl->SetPan = (HRESULT (STDMETHODCALLTYPE *)(LPDIRECTSOUNDBUFFER, LONG)) DSB_SetPan;
    lpVtbl->SetFrequency = (HRESULT (STDMETHODCALLTYPE *)(LPDIRECTSOUNDBUFFER, DWORD)) DSB_SetFrequency;
    lpVtbl->Stop = (HRESULT (STDMETHODCALLTYPE *)(LPDIRECTSOUNDBUFFER)) DSB_Stop;
    lpVtbl->Unlock = (HRESULT (STDMETHODCALLTYPE *)(LPDIRECTSOUNDBUFFER, LPVOID, DWORD, LPVOID, DWORD)) DSB_Unlock;
    lpVtbl->Restore = (HRESULT (STDMETHODCALLTYPE *)(LPDIRECTSOUNDBUFFER)) DSB_Restore;

    w->lpVtbl = lpVtbl;
    w->real = real;
    w->ref = 1;
    return w;
}

HRESULT WINAPI DirectSoundCreate(LPCGUID lpcGuid, LPDIRECTSOUND *ppDS, LPUNKNOWN lpu)
{
    LPDIRECTSOUND realDS;
    HRESULT hr;
    char szGuid[GUID_STRLEN];
    dump_guid(szGuid, lpcGuid);
    printf(("DirectSoundCreate: entry guid=%s out_wrapped=0x%p pUnkOuter=0x%p\n", szGuid, ppDS, lpu));

    LoadRealDSound();
    if (!pReal_DirectSoundCreate) {
        printf(("DirectSoundCreate returns DSERR_UNSUPPORTED\n"));
        return DSERR_UNSUPPORTED;
    }

    realDS = NULL;
    hr = pReal_DirectSoundCreate(lpcGuid, &realDS, lpu);
    if (FAILED(hr)) {
        printf(("DirectSoundCreate returns 0x%08lX\n", hr));
        return hr;
    }

    *ppDS = (LPDIRECTSOUND) WRAP_DirectSound(realDS);
    printf(("DirectSoundCreate: returned wrapped=0x%p\n", *ppDS));
    return S_OK;
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    OSVERSIONINFO osvi;
    printf(("DllMain: reason=%lu hinst=0x%p\n", fdwReason, hinstDLL));

    if (fdwReason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hinstDLL);
        osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
        GetVersionEx(&osvi);
#ifndef NDEBUG
        if (osvi.dwPlatformId == VER_PLATFORM_WIN32_WINDOWS) {
            g_fpDebug = fopen("dsound_wrapper.log", "wt");
            // enable g_fdDebug for hard lockup
            // g_fdDebug = _fileno(g_fpDebug);
        }
#endif
    } else if (fdwReason == DLL_PROCESS_DETACH) {
        if (g_hRealDSound) {
            FreeLibrary(g_hRealDSound);
            g_hRealDSound = NULL;
        }
#ifndef NDEBUG
        if (g_fpDebug != NULL) {
            fclose(g_fpDebug);
            g_fpDebug = NULL;
            g_fdDebug = -1;
        }
#endif
    }
    return TRUE;
}