#include <windows.h>
#include <stdio.h>
#include <conio.h>
#include <io.h>

#ifdef NDEBUG

#define printf(x)

#else

#define printf(x) DebugPrint x

static FILE *fpDebug = NULL;
static int fdDebug = -1;

static void DebugPrint(const char *format, ...) {
    va_list args;
    char buffer[1024];
    int i;
    va_start(args, format);
    if (fpDebug) {
        vfprintf(fpDebug, format, args);
        fflush(fpDebug);
        if (fdDebug != -1) {
            _commit(fdDebug);
        }
    } else {
        i = _vsnprintf(buffer, sizeof(buffer), format, args);
        buffer[i < 0 ? sizeof(buffer) - 1 : i] = '\0';
        OutputDebugStringA(buffer);
    }
    va_end(args);
}

#endif

// SB16 fixed resource configuration
#define SB16_BASE       0x220
// #define SB16_IRQ        5
#define SB16_DMA        1

// SB16 register offsets
#define DSP_RESET       (SB16_BASE + 0x06)
#define DSP_READ        (SB16_BASE + 0x0A)
#define DSP_WRITE       (SB16_BASE + 0x0C)
#define DSP_STATUS      (SB16_BASE + 0x0C)
#define DSP_DATA_AVAIL  (SB16_BASE + 0x0E)

// 8237 DMA controller registers (8-bit DMA channel 1)
#define DMA1_ADDR       0x02
#define DMA1_COUNT      0x03
#define DMA_PAGE1       0x83
#define DMA_MASK        0x0F
#define DMA_MODE        0x0B
#define DMA_CLEAR       0x0C

#define DMA_BOUNDARY    0x10000
#define DMA_BOUNDARY_1  0xFFFF
#define MAX_LENGTH      DMA_BOUNDARY_1

#define SAMPLE_RATE     11025

#define PHYS_ADDR_UNAVAILABLE 0xFFFFFFFF

typedef DWORD (__stdcall *PFN_VXDCALL)(DWORD, DWORD, DWORD);

/*
DSB16MEM.COM should be loaded in AUTOEXEC.BAT
MOV AX,ES
MOV DX,AX
ADD DX,2012
AND DX,F000
SUB DX,AX
MOV AX,3100
INT 21
MOV AX,4C01
INT 21
DB  "DSB16MEM"
*/
static const unsigned char cszMemTag[16] = {
    0x31, 0xCD, 0x21, 0xB8, 0x01, 0x4C, 0xCD, 0x21, 0x44, 0x53, 0x42, 0x31, 0x36, 0x4D, 0x45, 0x4D
};

// unchecked = 0, available = valid memory (64K alianged), unavailable = 0xFFFFFFFF;
static DWORD dwDmaBufferPhys = 0;
static LPVOID lpDmaBufferVirt = NULL;

static BOOL IsAvailable() {
    HMODULE hKernel32;
    PIMAGE_DOS_HEADER pDosHeader;
    PIMAGE_NT_HEADERS pNtHeaders;
    PIMAGE_EXPORT_DIRECTORY pExportDir;
    PFN_VXDCALL pVxDCall;
    DWORD dwRVA;
    WORD wax, wbx, wcx;
    BOOLEAN bcf;
    DWORD dwAddr, dwOffset;

    if (dwDmaBufferPhys) {
        return dwDmaBufferPhys != PHYS_ADDR_UNAVAILABLE;
    }

#ifndef NDEBUG
    fpDebug = fopen("dsb16_9x.log", "wt");
    // enable fdDebug for hard lockup, but may interrupt playback in v86 emulator
    // fdDebug = _fileno(fpDebug);
#endif

    hKernel32 = GetModuleHandle("KERNEL32.DLL");
    if (!hKernel32) {
        printf(("Unable to get handler for KERNEL32.DLL, error = %lu\n", GetLastError()));
        dwDmaBufferPhys = PHYS_ADDR_UNAVAILABLE;
        return FALSE;
    }
    // avoid compatibility mode
    if (!GetProcAddress(hKernel32, "QT_Thunk")) {
        dwDmaBufferPhys = PHYS_ADDR_UNAVAILABLE;
        return FALSE;
    }

    // refer to https://github.com/starofrainnight/winio/blob/v2.x/Source/Dll/k32exp.c#L38
    pDosHeader = (PIMAGE_DOS_HEADER) hKernel32;
    if (pDosHeader->e_magic != IMAGE_DOS_SIGNATURE) {
        printf(("Invalid magic %04x\n", pDosHeader->e_magic));
        dwDmaBufferPhys = PHYS_ADDR_UNAVAILABLE;
        return FALSE;
    }
    pNtHeaders = (PIMAGE_NT_HEADERS) ((LPBYTE) pDosHeader + pDosHeader->e_lfanew);
    dwRVA = pNtHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress;
    pExportDir = (PIMAGE_EXPORT_DIRECTORY) ((LPBYTE) pDosHeader + dwRVA);
    dwRVA = ((LPDWORD) ((LPBYTE) pDosHeader + pExportDir->AddressOfFunctions))[0];
    pVxDCall = (PFN_VXDCALL) ((LPBYTE) pDosHeader + dwRVA);

    // refer to https://github.com/starofrainnight/winio/blob/v2.x/Source/Dll/Phys32.cpp#L51
#ifdef _MSC_VER
    __asm {
        mov bx, 0;
        mov cx, 0;
        mov si, 0x10;
        mov di, 0;
    }
    pVxDCall(0x2A0029, 0x800, 0);
    __asm {
        setc bcf;
        mov wax, ax;
        mov wbx, bx;
        mov wcx, cx;
    }
#else
    __asm__ __volatile__ (
        "movw $0, %%bx\n\t"
        "movw $0, %%cx\n\t"
        "movw $0x10, %%si\n\t"
        "movw $0, %%di\n\t"
        :
        :
        : "bx", "cx", "si", "di"
    );
    pVxDCall(0x2A0029, 0x800, 0);
    __asm__ __volatile__ (
        "setcb %0\n\t"
        : "=m" (bcf), "=a" (wax), "=b" (wbx), "=c" (wcx)
        :
    );
#endif
    if (bcf) {
        printf(("Failed to call _MapPhysToLinear, error = 0x%04X", wax));
        dwDmaBufferPhys = PHYS_ADDR_UNAVAILABLE;
        return FALSE;
    }
    dwAddr = ((DWORD) wbx << 16) + wcx;
    printf(("_MapPhysToLinear returns 0x%08lX\n", dwAddr));

    for (dwOffset = 0; dwOffset < 0xF0000; dwOffset += 16) {
        if (memcmp((LPCVOID) (dwAddr + dwOffset), cszMemTag, 16) == 0) {
            break;
        }
    }
    if (dwOffset >= 0xF0000) {
        printf(("DSB16MEM Not Found\n"));
        dwDmaBufferPhys = PHYS_ADDR_UNAVAILABLE;
        return FALSE;
    }
    printf(("DSB16MEM Found at 0x%08lX\n", dwAddr + dwOffset));
    dwDmaBufferPhys = (dwOffset + 16 + DMA_BOUNDARY_1) & ~DMA_BOUNDARY_1;
    lpDmaBufferVirt = (LPVOID) (dwAddr + dwDmaBufferPhys);
    return TRUE;
}

// Write a byte to the DSP data port with basic protection and logging.
static VOID DspWrite(BYTE ucValue) {
    DWORD dwTimeout = 1000;
    // Wait until DSP is ready to accept a byte (bit7 clear)
    while ((_inp(DSP_STATUS) & 0x80) && --dwTimeout) {
        Sleep(0);
    }
    if (dwTimeout == 0) {
        printf(("DirectSB16: DspWrite timeout waiting for DSP_STATUS. Ignored byte: 0x%02X.\n", ucValue));
    } else {
        printf(("DirectSB16: Waited %lu microseconds before write 0x%02X.\n", (1000 - dwTimeout)*10, ucValue));
        _outp(DSP_WRITE, ucValue);
    }
}

// Reset the DSP and verify presence. Returns TRUE on success.
static BOOL DspReset(void) {
    DWORD dwTimeout;
    BYTE ucResp;

    _outp(DSP_RESET, 1);
    Sleep(0);
    _outp(DSP_RESET, 0);
    Sleep(0);

    // Read response with timeout
    // Read a byte from the DSP data port with timeout and logging.
    dwTimeout = 1000;
    while (!(_inp(DSP_DATA_AVAIL) & 0x80) && --dwTimeout) {
        Sleep(0);
    }
    if (dwTimeout == 0) {
        printf(("DirectSB16: DspRead timeout waiting for data available.\n"));
        return FALSE;
    }
    printf(("DirectSB16: Waited %lu microseconds after reset.\n", (1000 - dwTimeout)*10));
    ucResp = _inp(DSP_READ);
    if (ucResp != 0xAA) {
        printf(("DirectSB16: DSP reset failed, response 0x%02X.\n", ucResp));
        return FALSE;
    }
    printf(("DirectSB16: DSP reset successful (0xAA).\n"));
    return TRUE;
}

BOOL DirectSB16Write(LPVOID lpUserBuffer, DWORD dwBytesToWrite) {
    DWORD dwBytesWritten;
    BYTE ucPage;
    WORD wCount;

    if (!IsAvailable()) {
        printf(("DirectSB16: Writing for Win9x is unavailable\n"));
        return FALSE;
    }
    dwBytesWritten = dwBytesToWrite;
    // Limit single write size (ISA DMA counter limit is 64KB)
    if (dwBytesWritten > MAX_LENGTH) {
        printf(("DirectSB16: Write length %lu too large, truncated to %u\n", dwBytesWritten, MAX_LENGTH));
        dwBytesWritten = MAX_LENGTH;
    }

    // Ensure hardware clean
    if (!DspReset()) {
        printf(("DirectSB16: DspReset failed before playback.\n"));
        return FALSE;
    }
    // Clear any pending IRQ/status
    DspWrite(0xD0);
    Sleep(0);
    _inp(DSP_STATUS);
    _inp(DSP_READ);
    _inp(DSP_DATA_AVAIL);
    Sleep(0);

    memcpy(lpDmaBufferVirt, lpUserBuffer, dwBytesWritten);

    // Program DMA and DSP under protection {{
    ucPage = (BYTE) ((dwDmaBufferPhys >> 16) & 0xFF);
    // 64K aligned, so wOffset == 0
    // wOffset = (WORD) (dwDmaBufferPhys & 0xFFFF);
    wCount = (WORD) (dwBytesWritten - 1);

    // Mask channel 1 (mask value: channel + mask bit)
    _outp(DMA_MASK, 0x05);  // mask channel 1
    // Clear flip-flop
    _outp(DMA_CLEAR, 0x00);
    // Mode: single transfer, increment, read from memory (to device), channel 1
    _outp(DMA_MODE, 0x49);

    // Write address (low then high)
    _outp(DMA1_ADDR, 0 /* (BYTE) (wOffset & 0xFF) */);
    _outp(DMA1_ADDR, 0 /* (BYTE) ((wOffset >> 8) & 0xFF) */);

    // Write count (low then high)
    _outp(DMA1_COUNT, (BYTE) (wCount & 0xFF));
    _outp(DMA1_COUNT, (BYTE) ((wCount >> 8) & 0xFF));

    // Write page register
    _outp(DMA_PAGE1, ucPage);

    // Unmask channel 1
    _outp(DMA_MASK, 0x01);
    // }}

    // Set sample rate: 11025 Hz (command 0x41 followed by 2-byte rate)
    DspWrite(0x41);
    DspWrite(SAMPLE_RATE >> 8);
    DspWrite(SAMPLE_RATE & 0xFF);

    // Start 8-bit single-cycle DMA playback
    DspWrite(0xC0); // 8-bit single-cycle output command
    DspWrite(0x00); // mode: mono, unsigned
    DspWrite((BYTE) ((dwBytesWritten - 1) & 0xFF));
    DspWrite((BYTE) (((dwBytesWritten - 1) >> 8) & 0xFF));
    printf(("DirectSB16: Playback started.\n"));
    return TRUE;
}