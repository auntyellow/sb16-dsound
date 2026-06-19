#include <ntddk.h>

#ifdef NDEBUG
  #define printf(x)
#else
  #define printf(x) DbgPrint x
#endif

#define DEVICE_NAME     L"\\Device\\DirectSB16"
#define DOS_DEVICE_NAME L"\\DosDevices\\DirectSB16"

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

typedef struct {
#ifdef SB16_IRQ
  KEVENT PlaybackEvent;
  PKINTERRUPT InterruptObject;
#endif
  // PADAPTER_OBJECT pDmaAdapter;
  PVOID dmaBuffer, dmaBufferAligned;
  PHYSICAL_ADDRESS physAddr, physAddrAligned;
} DIRECT_SB16_EXT;

NTSTATUS Stub(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
  PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(Irp);
  printf(("Stub: %d\n", stack->MajorFunction));
  IoCompleteRequest(Irp, IO_NO_INCREMENT);
  return STATUS_SUCCESS;
}

// Write a byte to the DSP data port with basic protection and logging.
VOID DspWrite(UCHAR value) {
  ULONG timeout;
  // Wait until DSP is ready to accept a byte (bit7 clear)
  timeout = 1000;
  while ((READ_PORT_UCHAR((PUCHAR) DSP_STATUS) & 0x80) && --timeout) {
    KeStallExecutionProcessor(10);
  }
  if (timeout == 0) {
    printf(("DirectSB16: DspWrite timeout waiting for DSP_STATUS. Ignored byte: 0x%02X.\n", value));
  } else {
    printf(("DirectSB16: Waited %u microseconds before write 0x%02X.\n", (1000 - timeout)*10, value));
    WRITE_PORT_UCHAR((PUCHAR) DSP_WRITE, value);
  }
}

// Reset the DSP and verify presence. Returns TRUE on success.
BOOLEAN DspReset(void) {
  ULONG timeout;
  UCHAR resp;

  WRITE_PORT_UCHAR((PUCHAR) DSP_RESET, 1);
  KeStallExecutionProcessor(10);
  WRITE_PORT_UCHAR((PUCHAR) DSP_RESET, 0);
  KeStallExecutionProcessor(10);

  // Read response with timeout
  // Read a byte from the DSP data port with timeout and logging.
  timeout = 1000;
  while (!(READ_PORT_UCHAR((PUCHAR) DSP_DATA_AVAIL) & 0x80) && --timeout) {
    KeStallExecutionProcessor(10);
  }
  if (timeout == 0) {
    printf(("DirectSB16: DspRead timeout waiting for data available.\n"));
    return FALSE;
  }
  printf(("DirectSB16: Waited %u microseconds after reset.\n", (1000 - timeout)*10));
  resp = READ_PORT_UCHAR((PUCHAR) DSP_READ);
  if (resp != 0xAA) {
    printf(("DirectSB16: DSP reset failed, response 0x%02X.\n", resp));
    return FALSE;
  }
  printf(("DirectSB16: DSP reset successful (0xAA).\n"));
  return TRUE;
}

#ifdef SB16_IRQ
// Note: ISR should return TRUE only if this device caused the interrupt.
// We perform a minimal read to acknowledge and then signal the playback event.
BOOLEAN Sb16InterruptService(PKINTERRUPT Interrupt, PVOID ServiceContext) {
  // Read data-available/status to determine if this IRQ is for SB16.
  UCHAR dataAvail = READ_PORT_UCHAR((PUCHAR) DSP_DATA_AVAIL);
  // SB16 signals interrupt with bit7 set in DSP_DATA_AVAIL (as used earlier).
  if (!(dataAvail & 0x80)) {
    // Not our interrupt
    printf(("Not our interrupt, dataAvail = 0x%02X\n", dataAvail));
    return FALSE;
  }
  // Acknowledge/clear the device interrupt by reading DSP_READ (consumes data)
  // and reading DSP_STATUS to ensure device internal flags are cleared.
  // Some cards require reading DSP_READ; some require reading DSP_STATUS first.
  // Do both to be robust.
  READ_PORT_UCHAR((PUCHAR) DSP_READ);
  READ_PORT_UCHAR((PUCHAR) DSP_STATUS);
  // Wake up the waiting writer thread (playback completion)
  KeSetEvent((KEVENT *) ServiceContext, IO_NO_INCREMENT, FALSE);
  printf(("Our interrupt, dataAvail = 0x%02X\n", dataAvail));
  return TRUE;
}
#endif

VOID DspClear() {
  // Clear any pending IRQ/status
  DspWrite(0xD0);
  KeStallExecutionProcessor(10);
  READ_PORT_UCHAR((PUCHAR) DSP_STATUS);
  READ_PORT_UCHAR((PUCHAR) DSP_READ);
  READ_PORT_UCHAR((PUCHAR) DSP_DATA_AVAIL);
  KeStallExecutionProcessor(10);
}

NTSTATUS DirectWrite(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
  ULONG length;
  PVOID userBuffer;
  DIRECT_SB16_EXT *ext;
  UCHAR page;
  USHORT /* offset, */ count;

  length = IoGetCurrentIrpStackLocation(Irp)->Parameters.Write.Length;
  userBuffer = Irp->AssociatedIrp.SystemBuffer;
  ext = (DIRECT_SB16_EXT *) DeviceObject->DeviceExtension;

  printf(("DirectSB16: Write called, length=%u, userBuffer=0x%p\n", length, userBuffer));

  if (length == 0 || userBuffer == NULL) {
    printf(("DirectSB16: Write called with zero length or NULL buffer.\n"));
    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
  }

  // Limit single write size (ISA DMA counter limit is 64KB)
  if (length > MAX_LENGTH) {
    printf(("DirectSB16: Write length %u too large, truncated to %u\n", length, MAX_LENGTH));
    length = MAX_LENGTH;
  }

  __try {
    // Ensure hardware clean
    if (!DspReset()) {
      printf(("DirectSB16: DspReset failed before playback.\n"));
      Irp->IoStatus.Status = STATUS_UNSUCCESSFUL;
      Irp->IoStatus.Information = 0;
      IoCompleteRequest(Irp, IO_NO_INCREMENT);
      return STATUS_UNSUCCESSFUL;
    }
    DspClear();

    RtlCopyMemory(ext->dmaBufferAligned, userBuffer, length);

    // Program DMA and DSP under protection {{
    page = (UCHAR) ((ext->physAddrAligned.LowPart >> 16) & 0xFF);
    // 64K aligned, so offset == 0
    // offset = (USHORT) (ext->physAddrAligned.LowPart & 0xFFFF);
    count = (USHORT) (length - 1);

    // Mask channel 1 (mask value: channel + mask bit)
    WRITE_PORT_UCHAR((PUCHAR) DMA_MASK, 0x05);  // mask channel 1
    // Clear flip-flop
    WRITE_PORT_UCHAR((PUCHAR) DMA_CLEAR, 0x00);
    // Mode: single transfer, increment, read from memory (to device), channel 1
    WRITE_PORT_UCHAR((PUCHAR) DMA_MODE, 0x49);

    // Write address (low then high)
    WRITE_PORT_UCHAR((PUCHAR) DMA1_ADDR, 0 /* (UCHAR)(offset & 0xFF) */);
    WRITE_PORT_UCHAR((PUCHAR) DMA1_ADDR, 0 /* (UCHAR)((offset >> 8) & 0xFF) */);

    // Write count (low then high)
    WRITE_PORT_UCHAR((PUCHAR) DMA1_COUNT, (UCHAR)(count & 0xFF));
    WRITE_PORT_UCHAR((PUCHAR) DMA1_COUNT, (UCHAR)((count >> 8) & 0xFF));

    // Write page register
    WRITE_PORT_UCHAR((PUCHAR) DMA_PAGE1, page);

    // Unmask channel 1
    WRITE_PORT_UCHAR((PUCHAR) DMA_MASK, 0x01);
    // }}

    // Set sample rate: 11025 Hz (command 0x41 followed by 2-byte rate)
    DspWrite(0x41);
    DspWrite(SAMPLE_RATE >> 8);
    DspWrite(SAMPLE_RATE & 0xFF);

    // Start 8-bit single-cycle DMA playback
    DspWrite(0xC0); // 8-bit single-cycle output command
    DspWrite(0x00); // mode: mono, unsigned
    DspWrite((UCHAR) ((length - 1) & 0xFF));
    DspWrite((UCHAR) (((length - 1) >> 8) & 0xFF));
    printf(("DirectSB16: Playback started.\n"));

    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = length;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
  } __except (
    DbgPrint("Exception Addr: 0x%p\n", ((PEXCEPTION_POINTERS) GetExceptionInformation())->ExceptionRecord->ExceptionAddress),
    EXCEPTION_EXECUTE_HANDLER
  ) {
    printf(("DirectSB16: Exception during DirectWrite.\n"));
    Irp->IoStatus.Status = STATUS_UNSUCCESSFUL;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_UNSUCCESSFUL;
  }
}

VOID DriverUnload(PDRIVER_OBJECT DriverObject) {
  UNICODE_STRING symLink;
  PDEVICE_OBJECT deviceObject = DriverObject->DeviceObject;
  DIRECT_SB16_EXT *ext = (DIRECT_SB16_EXT *) deviceObject->DeviceExtension;

  __try {
    if (!DspReset()) {
      printf(("DirectSB16: DspReset failed before unload.\n"));
    }
    DspClear();
  } __except (
    DbgPrint("Exception Addr: 0x%p\n", ((PEXCEPTION_POINTERS) GetExceptionInformation())->ExceptionRecord->ExceptionAddress),
    EXCEPTION_EXECUTE_HANDLER
  ) {
    printf(("DirectSB16: Exception during DspReset.\n"));
  }

#ifdef SB16_IRQ
  if (ext->InterruptObject != NULL) {
    IoDisconnectInterrupt(ext->InterruptObject);
    ext->InterruptObject = NULL;
  }
#endif
  if (/* ext->pDmaAdapter != NULL && */ ext->dmaBuffer != NULL) {
    MmFreeContiguousMemory(ext->dmaBuffer);
    // HalFreeCommonBuffer(ext->pDmaAdapter, DMA_BOUNDARY, ext->physAddr, ext->dmaBuffer, FALSE);
    // ext->pDmaAdapter = NULL;
    ext->dmaBuffer = NULL;
  }

  RtlInitUnicodeString(&symLink, DOS_DEVICE_NAME);
  IoDeleteSymbolicLink(&symLink);
  IoDeleteDevice(deviceObject);
  printf(("DirectSB16: Driver unloaded.\n"));
}

NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath) {
  UNICODE_STRING devName, symLink;
  NTSTATUS status;
  PDEVICE_OBJECT deviceObject;
  DIRECT_SB16_EXT *ext;
  PHYSICAL_ADDRESS low, high, boundary;
  ULONG offset;
  // DEVICE_DESCRIPTION deviceDesc;
#ifdef SB16_IRQ
  ULONG vector;
  KIRQL irql;
  KAFFINITY affinity;
#endif
  ULONG i;

  RtlInitUnicodeString(&devName, DEVICE_NAME);
  status = IoCreateDevice(DriverObject, sizeof(DIRECT_SB16_EXT), &devName, FILE_DEVICE_UNKNOWN, 0, FALSE, &deviceObject);
  if (!NT_SUCCESS(status)) {
    printf(("DirectSB16: IoCreateDevice failed 0x%08X\n", status));
    return status;
  }
  ext = (DIRECT_SB16_EXT *) deviceObject->DeviceExtension;
#ifdef SB16_IRQ
  ext->InterruptObject = NULL;
#endif
  ext->dmaBuffer = NULL;
  // Use buffered I/O so SystemBuffer is available
  deviceObject->Flags |= DO_BUFFERED_IO;

  RtlInitUnicodeString(&symLink, DOS_DEVICE_NAME);
  // Defensive: attempt to delete any stale symbolic link, ignore failure
  IoDeleteSymbolicLink(&symLink);
  status = IoCreateSymbolicLink(&symLink, &devName);
  if (!NT_SUCCESS(status)) {
    printf(("DirectSB16: IoCreateSymbolicLink failed 0x%08X\n", status));
    IoDeleteDevice(deviceObject);
    return status;
  }

  printf(("DirectSB16: DriverEntry started.\n"));

  __try {
#ifdef SB16_IRQ
    // Initialize the playback event once at driver load
    KeInitializeEvent(&(ext->PlaybackEvent), NotificationEvent, FALSE);
#endif

    // Try to reset DSP
    if (!DspReset()) {
      printf(("DirectSB16: DSP not present, aborting DriverEntry.\n"));
      IoDeleteSymbolicLink(&symLink);
      IoDeleteDevice(deviceObject);  
      return STATUS_DEVICE_DOES_NOT_EXIST;
    }

    low.QuadPart = 0;
    // ISA DMA must be below 16MB
    high.QuadPart = 0xFFFFFF;
    // ensure not crossing 64KB boundary
    boundary.QuadPart = DMA_BOUNDARY;

#ifdef NT351
    // Cached works fine
    ext->dmaBuffer = MmAllocateContiguousMemory(DMA_BOUNDARY*2, high);
#else
    ext->dmaBuffer = MmAllocateContiguousMemorySpecifyCache(DMA_BOUNDARY, low, high, boundary, MmNonCached);
#endif
    if (ext->dmaBuffer == NULL) {
      printf(("DirectSB16: MmAllocateContiguousMemorySpecifyCache failed.\n"));
      IoDeleteSymbolicLink(&symLink);
      IoDeleteDevice(deviceObject);
      return STATUS_INSUFFICIENT_RESOURCES;
    }
    ext->physAddr = MmGetPhysicalAddress(ext->dmaBuffer);

    // doesn't work for 0x20000
    /*
    RtlZeroMemory(&deviceDesc, sizeof(DEVICE_DESCRIPTION));
    deviceDesc.Version = DEVICE_DESCRIPTION_VERSION;
    deviceDesc.InterfaceType = Isa;
    deviceDesc.DmaChannel = 1;
    deviceDesc.DmaWidth = Width8Bits;
    deviceDesc.DmaSpeed = Compatible;
    deviceDesc.Dma32BitAddresses = FALSE;
    deviceDesc.ScatterGather = FALSE;
    deviceDesc.Master = FALSE;
    deviceDesc.MaximumLength = DMA_BOUNDARY;
    deviceDesc.DemandMode = TRUE;
    ext->pDmaAdapter = HalGetAdapter(&deviceDesc, &i);
    if (ext->pDmaAdapter == NULL) {
      printf(("DirectSB16: Unable to get DMA adapter.\n"));
      IoDeleteSymbolicLink(&symLink);
      IoDeleteDevice(deviceObject);
      return STATUS_INSUFFICIENT_RESOURCES;
    }

    ext->dmaBuffer = HalAllocateCommonBuffer(ext->pDmaAdapter, DMA_BOUNDARY, &ext->physAddr, FALSE);
    if (ext->dmaBuffer == NULL) {
      printf(("DirectSB16: HalAllocateCommonBuffer failed.\n"));
      ext->pDmaAdapter = NULL;
      IoDeleteSymbolicLink(&symLink);
      IoDeleteDevice(deviceObject);
      return STATUS_INSUFFICIENT_RESOURCES;
    }
    */

#ifdef NT351
    ext->physAddr = MmGetPhysicalAddress(ext->dmaBuffer);
    offset = ((ext->physAddr.LowPart + DMA_BOUNDARY_1) & ~DMA_BOUNDARY_1) - ext->physAddr.LowPart;
    ext->dmaBufferAligned = (PUCHAR) ext->dmaBuffer + offset;
    ext->physAddrAligned.QuadPart = ext->physAddr.QuadPart + offset;
    printf(("DirectSB16: Allocated 128KB from 0x%08X, 64KB aligned from 0x%08X.\n", ext->physAddr.LowPart, ext->physAddrAligned.LowPart));
#else
    UNREFERENCED_PARAMETER(offset);
    // verify within 64KB boundary, doesn't work under Windows NT 3.51
    if ((ext->physAddr.LowPart & DMA_BOUNDARY_1) != 0) {
      printf(("DirectSB16: DMA buffer crosses 64KB boundary, must split.\n"));
      MmFreeContiguousMemory(ext->dmaBuffer);
      // HalFreeCommonBuffer(ext->pDmaAdapter, DMA_BOUNDARY, ext->physAddr, ext->dmaBuffer, FALSE);
      // ext->pDmaAdapter = NULL;
      ext->dmaBuffer = NULL;
      IoDeleteSymbolicLink(&symLink);
      IoDeleteDevice(deviceObject);
      return STATUS_INSUFFICIENT_RESOURCES;
    }
    ext->dmaBufferAligned = ext->dmaBuffer;
    ext->physAddrAligned.QuadPart = ext->physAddr.QuadPart;
    printf(("DirectSB16: Allocated 64KB from 0x%08X.\n", ext->physAddr.LowPart));
#endif

#ifdef SB16_IRQ
    // Connect hardware interrupt (IRQ 5)
    vector = HalGetInterruptVector(Isa, 0, SB16_IRQ, SB16_IRQ, &irql, &affinity);
    if (vector == 0) {
      printf(("DirectSB16: HalGetInterruptVector failed for IRQ %u.\n", SB16_IRQ));
      MmFreeContiguousMemory(ext->dmaBuffer);
      // HalFreeCommonBuffer(ext->pDmaAdapter, DMA_BOUNDARY, ext->physAddr, ext->dmaBuffer, FALSE);
      // ext->pDmaAdapter = NULL;
      ext->dmaBuffer = NULL;
      IoDeleteSymbolicLink(&symLink);
      IoDeleteDevice(deviceObject);
      return STATUS_INSUFFICIENT_RESOURCES;
    }
    status = IoConnectInterrupt(&(ext->InterruptObject), Sb16InterruptService, &(ext->PlaybackEvent), NULL, vector, irql, irql, Latched, FALSE, affinity, FALSE);
    if (!NT_SUCCESS(status)) {
      printf(("DirectSB16: IoConnectInterrupt failed 0x%08X\n", status));
      MmFreeContiguousMemory(ext->dmaBuffer);
      // HalFreeCommonBuffer(ext->pDmaAdapter, DMA_BOUNDARY, ext->physAddr, ext->dmaBuffer, FALSE);
      // ext->pDmaAdapter = NULL;
      ext->dmaBuffer = NULL;
      IoDeleteSymbolicLink(&symLink);
      IoDeleteDevice(deviceObject);
      return status;
    }
#endif
  } __except (
    DbgPrint("Exception Addr: 0x%p\n", ((PEXCEPTION_POINTERS) GetExceptionInformation())->ExceptionRecord->ExceptionAddress),
    EXCEPTION_EXECUTE_HANDLER
  ) {
    printf(("DirectSB16: Exception occurs.\n"));
    status = STATUS_UNSUCCESSFUL;
  }

  for (i = 0; i < IRP_MJ_MAXIMUM_FUNCTION; i++) {
    DriverObject->MajorFunction[i] = Stub;
  }
  DriverObject->MajorFunction[IRP_MJ_WRITE] = DirectWrite;
  DriverObject->DriverUnload = DriverUnload;
  deviceObject->Flags &= ~DO_DEVICE_INITIALIZING;

  printf(("DirectSB16: DriverEntry completed successfully.\n"));
  return STATUS_SUCCESS;
}
