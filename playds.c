#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <dsound.h>

// Stereo, 44100Hz, 16-bit (65536*16) -> Mono, 11025Hz, 8-bit (65536)
#define BUFFER_SIZE (65536*16)

// Standard WAV file header structure (packed)
#pragma pack(push, 1)
typedef struct {
    char  riff[4];          // "RIFF"
    DWORD riffSize;
    char  wave[4];          // "WAVE"
    char  fmt[4];           // "fmt "
    DWORD fmtSize;
    PCMWAVEFORMAT wf;
} WAVHEADER;
// extra bytes
typedef struct {
    char  chunkTag[4];      // "data"
    DWORD chunkSize;        // size of raw PCM data
} CHUNKHEADER;
#pragma pack(pop)

BYTE pcmBuffer[BUFFER_SIZE];

int main(int argc, char* argv[]) {
    FILE *fp;
    WAVHEADER header;
    CHUNKHEADER header2;
    DWORD bytesRead;

    LPDIRECTSOUND pDS;
    LPDIRECTSOUNDBUFFER pDSB;
    DSBUFFERDESC dsbd;
    WAVEFORMATEX wfx;
    LPVOID pBufferData1, pBufferData2;
    DWORD dwBufferLength1, dwBufferLength2, dwStatus, dwPlay, dwWrite, i;

    if (argc < 2) {
        printf("Usage: playds <filename.wav>\n");
        return 1;
    }

    // 1. Open and read WAV file
    fp = fopen(argv[1], "rb");
    if (!fp) {
        printf("Error: Cannot open file %s\n", argv[1]);
        return 1;
    }

    bytesRead = fread(&header, 1, sizeof(WAVHEADER), fp);
    if (bytesRead != sizeof(WAVHEADER)) {
        printf("Error: Failed to read WAV header (read %lu bytes).\n", bytesRead);
        fclose(fp);
        return 1;
    }

    // 2. Strictly validate audio format
    if (strncmp(header.riff, "RIFF", 4) != 0 || strncmp(header.wave, "WAVE", 4) != 0) {
        printf("Error: Not a valid WAV file.\n");
        fclose(fp);
        return 1;
    }

    printf("format: %lu bytes, %u(%s), %u-channel, %luHz, %u-bit\n", header.fmtSize,
            header.wf.wf.wFormatTag, header.wf.wf.wFormatTag == WAVE_FORMAT_PCM ? "PCM" : "Non-PCM",
            header.wf.wf.nChannels, header.wf.wf.nSamplesPerSec, header.wf.wBitsPerSample);
    if (header.wf.wf.wFormatTag != WAVE_FORMAT_PCM) {
        printf("Error: Format mismatch! Only PCM is supported.\n");
        fclose(fp);
        return 1;
    }

    if (header.fmtSize > 16) {
        printf("Skip %lu extra bytes.\n", header.fmtSize - 16);
        fseek(fp, header.fmtSize - 16, SEEK_CUR);
    }
    while (TRUE) {
        bytesRead = fread(&header2, 1, sizeof(CHUNKHEADER), fp);
        if (bytesRead != sizeof(CHUNKHEADER)) {
            printf("Error: Failed to read chunk header (read %lu bytes).\n", bytesRead);
            fclose(fp);
            return 1;
        }
        printf("%.4s chunk: %lu bytes\n", header2.chunkTag, header2.chunkSize);
        if (strncmp(header2.chunkTag, "data", 4) == 0) {
            break;
        }
        fseek(fp, header2.chunkSize, SEEK_CUR);
    }

    // Validate chunkSize before allocation
    if (header2.chunkSize == 0) {
        printf("Error: WAV file contains no PCM data.\n");
        fclose(fp);
        return 1;
    }

    if (header2.chunkSize > BUFFER_SIZE) {
        printf("Truncated to %u bytes.\n", BUFFER_SIZE);
        header2.chunkSize = BUFFER_SIZE;
    }
    bytesRead = fread(pcmBuffer, 1, header2.chunkSize, fp);
    if (bytesRead != header2.chunkSize) {
        printf("Warning: Expected %lu bytes but read %lu bytes of PCM data.\n", header2.chunkSize, bytesRead);
    }
    fclose(fp);

    // 3. Init DirectSound
    if (FAILED(DirectSoundCreate(NULL, &pDS, NULL))) {
        printf("Error: Cannot initialize DirectSound.\n");
        return 1;
    }
    if (FAILED(IDirectSound_SetCooperativeLevel(pDS, GetDesktopWindow(), DSSCL_NORMAL))) {
        printf("Error: SetCooperativeLevel failed.\n");
        IDirectSound_Release(pDS);
        return 1;
    }

    memset(&wfx, 0, sizeof(WAVEFORMATEX));
    wfx.wFormatTag      = WAVE_FORMAT_PCM;
    wfx.nChannels       = header.wf.wf.nChannels;
    wfx.nSamplesPerSec  = header.wf.wf.nSamplesPerSec;
    wfx.wBitsPerSample  = header.wf.wBitsPerSample;
    wfx.nBlockAlign     = (wfx.nChannels*wfx.wBitsPerSample)/8;
    wfx.nAvgBytesPerSec = wfx.nSamplesPerSec*wfx.nBlockAlign;
    wfx.cbSize          = 0;
    memset(&dsbd, 0, sizeof(DSBUFFERDESC));
    dsbd.dwSize         = sizeof(DSBUFFERDESC);
    dsbd.dwFlags        = DSBCAPS_GLOBALFOCUS;
    dsbd.dwBufferBytes  = bytesRead;
    dsbd.lpwfxFormat    = &wfx;
    if (FAILED(IDirectSound_CreateSoundBuffer(pDS, &dsbd, &pDSB, NULL))) {
        printf("Error: CreateSoundBuffer failed.\n");
        IDirectSound_Release(pDS);
        return 1;
    }

    // 4. Write into DirectSoundBuffer
    if (FAILED(IDirectSoundBuffer_Lock(pDSB, 0, bytesRead, &pBufferData1, &dwBufferLength1, &pBufferData2, &dwBufferLength2, 0))) {
        printf("Error: Lock Sound Buffer failed.\n");
        IDirectSoundBuffer_Release(pDSB);
        IDirectSound_Release(pDS);
        return 1;
    }
    memcpy(pBufferData1, pcmBuffer, dwBufferLength1);
    if (pBufferData2 != NULL) {
        memcpy(pBufferData2, pcmBuffer + dwBufferLength1, dwBufferLength2);
    }
    IDirectSoundBuffer_Unlock(pDSB, pBufferData1, dwBufferLength1, pBufferData2, dwBufferLength2);

    // 5. Play
    printf("Playing via DirectSound ...\n");
    if (FAILED(IDirectSoundBuffer_Play(pDSB, 0, 0, 0))) {
        printf("Error: Play failed.\n");
    } else {
        IDirectSoundBuffer_GetStatus(pDSB, &dwStatus);
        IDirectSoundBuffer_GetCurrentPosition(pDSB, &dwPlay, &dwWrite);
        printf("Status: %lu, Play: %lu, Write: %lu\n", dwStatus, dwPlay, dwWrite);
        printf("Wait for 10 seconds ...\n");
        for (i = 0; i < 100 && (dwStatus & DSBSTATUS_PLAYING) != 0; i ++) {
            Sleep(100);
            IDirectSoundBuffer_GetStatus(pDSB, &dwStatus);
            IDirectSoundBuffer_GetCurrentPosition(pDSB, &dwPlay, &dwWrite);
            printf("Status: %lu, Play: %lu, Write: %lu\n", dwStatus, dwPlay, dwWrite);
        }
        // IDirectSoundBuffer_Stop(pDSB);
        // printf("Stop\n");
    }

    IDirectSoundBuffer_Release(pDSB);
    IDirectSound_Release(pDS);
    return 0;
}