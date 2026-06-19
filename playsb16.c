#include <windows.h>
#include <stdio.h>
#include <string.h>

#define BUFFER_SIZE 65536

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

int main(int argc, char* argv[]) {
    FILE *fp;
    WAVHEADER header;
    CHUNKHEADER header2;
    DWORD bytesRead, bytesWritten;
    BYTE pcmBuffer[BUFFER_SIZE];
    HANDLE hDevice;

    if (argc < 2) {
        printf("Usage: playsb16 <filename.wav>\n");
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
    if (header.wf.wf.wFormatTag != WAVE_FORMAT_PCM || header.wf.wf.nChannels != 1 || header.wf.wf.nSamplesPerSec != 11025 || header.wf.wBitsPerSample != 8) {
        printf("Error: Format mismatch! Only PCM Mono 11025Hz 8-bit is supported.\n");
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

    // 3. Open DirectSB16 driver device
    hDevice = CreateFile("\\\\.\\DirectSB16", GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (hDevice == INVALID_HANDLE_VALUE) {
        printf("Error: Cannot open DirectSB16 driver. (Error code: %lu)\n", GetLastError());
        return 1;
    }

    // 4. Write to driver in chunks for playback (max 64KB per chunk)
    printf("Playing...\n");
    if (WriteFile(hDevice, pcmBuffer, bytesRead, &bytesWritten, NULL)) {
        printf("Playback Finished!\n");
    } else {
        printf("Error: WriteFile failed during playback. Code: %lu\n", GetLastError());
    }

    CloseHandle(hDevice);
    return 0;
}

