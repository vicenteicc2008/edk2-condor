/** @file

  The PCI Root Bridge header file.

Copyright (c) 1999 - 2016, Intel Corporation. All rights reserved.<BR>
This program and the accompanying materials
are licensed and made available under the terms and conditions of the BSD License
which accompanies this distribution.  The full text of the license may be found at
http://opensource.org/licenses/bsd-license.php

THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

**/

#ifndef _PCI_ROOT_BRIDGE_H_
#define _PCI_ROOT_BRIDGE_H_

#include <PiDxe.h>
#include <IndustryStandard/Acpi.h>
#include <IndustryStandard/Pci.h>

//
// Driver Consumed Protocol Prototypes
//
#include <Protocol/Metronome.h>
#include <Protocol/CpuIo2.h>
#include <Protocol/DevicePath.h>
#include <Protocol/PciRootBridgeIo.h>
#include <Library/DebugLib.h>
#include <Library/DevicePathLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DxeServicesTableLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/BaseLib.h>
#include <Library/PciSegmentLib.h>
#include "PciHostResource.h"


typedef enum {
  IoOperation,
  MemOperation,
  PciOperation
} OPERATION_TYPE;

#define MAP_INFO_SIGNATURE  SIGNATURE_32 ('_', 'm', 'a', 'p')
typedef struct {
  UINT32                                    Signature;
  LIST_ENTRY                                Link;
  EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_OPERATION Operation;
  UINTN                                     NumberOfBytes;
  UINTN                                     NumberOfPages;
  EFI_PHYSICAL_ADDRESS                      HostAddress;
  EFI_PHYSICAL_ADDRESS                      MappedHostAddress;
} MAP_INFO;
#define MAP_INFO_FROM_LINK(a) CR (a, MAP_INFO, Link, MAP_INFO_SIGNATURE)

#define PCI_ROOT_BRIDGE_SIGNATURE SIGNATURE_32 ('_', 'p', 'r', 'b')

typedef struct {
  UINT32                            Signature;
  LIST_ENTRY                        Link;
  EFI_HANDLE                        Handle;
  UINT64                            AllocationAttributes;
  UINT64                            Attributes;
  UINT64                            Supports;
  PCI_RES_NODE                      ResAllocNode[TypeMax];
  PCI_ROOT_BRIDGE_APERTURE          Bus;
  PCI_ROOT_BRIDGE_APERTURE          Io;
  PCI_ROOT_BRIDGE_APERTURE          Mem;
  PCI_ROOT_BRIDGE_APERTURE          PMem;
  PCI_ROOT_BRIDGE_APERTURE          MemAbove4G;
  PCI_ROOT_BRIDGE_APERTURE          PMemAbove4G;
  BOOLEAN                           DmaAbove4G;
  VOID                              *ConfigBuffer;
  EFI_DEVICE_PATH_PROTOCOL          *DevicePath;
  CHAR16                            *DevicePathStr;
  EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL   RootBridgeIo;

  BOOLEAN                           ResourceSubmitted;
  LIST_ENTRY                        Maps;
} PCI_ROOT_BRIDGE_INSTANCE;

#define ROOT_BRIDGE_FROM_THIS(a) CR (a, PCI_ROOT_BRIDGE_INSTANCE, RootBridgeIo, PCI_ROOT_BRIDGE_SIGNATURE)

#define ROOT_BRIDGE_FROM_LINK(a) CR (a, PCI_ROOT_BRIDGE_INSTANCE, Link, PCI_ROOT_BRIDGE_SIGNATURE)

/**

  Construct the Pci Root Bridge Io protocol.

  @param Protocol          -  Protocol to initialize.
  @param HostBridgeHandle  -  Handle to the HostBridge.

  @retval EFI_SUCCESS  -  Success.
  @retval Others       -  Fail.

**/
PCI_ROOT_BRIDGE_INSTANCE *
CreateRootBridge (
  IN PCI_ROOT_BRIDGE       *Bridge,
  IN EFI_HANDLE            HostBridgeHandle
  );

//
// Protocol Member Function Prototypes
//
/**

  Poll an address in memory mapped space until an exit condition is met
  or a timeout occurs.

  @param This     -  Pointer to EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL instance.
  @param Width    -  Width of the memory operation.
  @param Address  -  The base address of the memory operation.
  @param Mask     -  Mask used for polling criteria.
  @param Value    -  Comparison value used for polling exit criteria.
  @param Delay    -  Number of 100ns units to poll.
  @param Result   -  Pointer to the last value read from memory location.

  @retval EFI_SUCCESS            -  Success.
  @retval EFI_INVALID_PARAMETER  -  Invalid parameter found.
  @retval EFI_TIMEOUT            -  Delay expired before a match occurred.
  @retval EFI_OUT_OF_RESOURCES   -  Fail due to lack of resources.

**/
EFI_STATUS
EFIAPI
RootBridgeIoPollMem (
  IN  EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL        *This,
  IN  EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_WIDTH  Width,
  IN  UINT64                                 Address,
  IN  UINT64                                 Mask,
  IN  UINT64                                 Value,
  IN  UINT64                                 Delay,
  OUT UINT64                                 *Result
  )
;

/**

  Poll an address in I/O space until an exit condition is met
  or a timeout occurs.

  @param This     -  Pointer to EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL instance.
  @param Width    -  Width of I/O operation.
  @param Address  -  The base address of the I/O operation.
  @param Mask     -  Mask used for polling criteria.
  @param Value    -  Comparison value used for polling exit criteria.
  @param Delay    -  Number of 100ns units to poll.
  @param Result   -  Pointer to the last value read from memory location.

  @retval EFI_SUCCESS            -  Success.
  @retval EFI_INVALID_PARAMETER  -  Invalid parameter found.
  @retval EFI_TIMEOUT            -  Delay expired before a match occurred.
  @retval EFI_OUT_OF_RESOURCES   -  Fail due to lack of resources.

**/
EFI_STATUS
EFIAPI
RootBridgeIoPollIo (
  IN  EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL        *This,
  IN  EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_WIDTH  Width,
  IN  UINT64                                 Address,
  IN  UINT64                                 Mask,
  IN  UINT64                                 Value,
  IN  UINT64                                 Delay,
  OUT UINT64                                 *Result
  )
;

/**

  Allow read from memory mapped I/O space.

  @param This     -  Pointer to EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL instance.
  @param Width    -  The width of memory operation.
  @param Address  -  Base address of the memory operation.
  @param Count    -  Number of memory opeartion to perform.
  @param Buffer   -  The destination buffer to store data.

  @retval EFI_SUCCESS            -  Success.
  @retval EFI_INVALID_PARAMETER  -  Invalid parameter found.
  @retval EFI_OUT_OF_RESOURCES   -  Fail due to lack of resources.

**/
EFI_STATUS
EFIAPI
RootBridgeIoMemRead (
  IN     EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL        *This,
  IN     EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_WIDTH  Width,
  IN     UINT64                                 Address,
  IN     UINTN                                  Count,
  IN OUT VOID                                   *Buffer
  )
;

/**

  Allow write to memory mapped I/O space.

  @param This     -  Pointer to EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL instance.
  @param Width    -  The width of memory operation.
  @param Address  -  Base address of the memory operation.
  @param Count    -  Number of memory opeartion to perform.
  @param Buffer   -  The source buffer to write data from.

  @retval EFI_SUCCESS            -  Success.
  @retval EFI_INVALID_PARAMETER  -  Invalid parameter found.
  @retval EFI_OUT_OF_RESOURCES   -  Fail due to lack of resources.

**/
EFI_STATUS
EFIAPI
RootBridgeIoMemWrite (
  IN     EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL        *This,
  IN     EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_WIDTH  Width,
  IN     UINT64                                 Address,
  IN     UINTN                                  Count,
  IN OUT VOID                                   *Buffer
  )
;

/**

  Enable a PCI driver to read PCI controller registers in the
  PCI root bridge I/O space.

  @param This         -  A pointer to EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL
  @param Width        -  Signifies the width of the memory operation.
  @param UserAddress  -  The base address of the I/O operation.
  @param Count        -  The number of I/O operations to perform.
  @param UserBuffer   -  The destination buffer to store the results.

  @retval EFI_SUCCESS            -  The data was read from the PCI root bridge.
  @retval EFI_INVALID_PARAMETER  -  Invalid parameters found.
  @retval EFI_OUT_OF_RESOURCES   -  The request could not be completed due to a lack of
                            @retval resources.
**/
EFI_STATUS
EFIAPI
RootBridgeIoIoRead (
  IN     EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL        *This,
  IN     EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_WIDTH  Width,
  IN     UINT64                                 UserAddress,
  IN     UINTN                                  Count,
  IN OUT VOID                                   *UserBuffer
  )
;

/**

  Enable a PCI driver to write to PCI controller registers in the
  PCI root bridge I/O space.

  @param This         -  A pointer to EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL
  @param Width        -  Signifies the width of the memory operation.
  @param UserAddress  -  The base address of the I/O operation.
  @param Count        -  The number of I/O operations to perform.
  @param UserBuffer   -  The source buffer to write data from.

  @retval EFI_SUCCESS            -  The data was written to the PCI root bridge.
  @retval EFI_INVALID_PARAMETER  -  Invalid parameters found.
  @retval EFI_OUT_OF_RESOURCES   -  The request could not be completed due to a lack of
                            @retval resources.
**/
EFI_STATUS
EFIAPI
RootBridgeIoIoWrite (
  IN     EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL        *This,
  IN     EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_WIDTH  Width,
  IN     UINT64                                 UserAddress,
  IN     UINTN                                  Count,
  IN OUT VOID                                   *UserBuffer
  )
;

/**

  Copy one region of PCI root bridge memory space to be copied to
  another region of PCI root bridge memory space.

  @param This         -  A pointer to EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL instance.
  @param Width        -  Signifies the width of the memory operation.
  @param DestAddress  -  Destination address of the memory operation.
  @param SrcAddress   -  Source address of the memory operation.
  @param Count        -  Number of memory operations to perform.

  @retval EFI_SUCCESS            -  The data was copied successfully.
  @retval EFI_INVALID_PARAMETER  -  Invalid parameters found.
  @retval EFI_OUT_OF_RESOURCES   -  The request could not be completed due to a lack of
                            @retval resources.
**/
EFI_STATUS
EFIAPI
RootBridgeIoCopyMem (
  IN EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL          *This,
  IN EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_WIDTH    Width,
  IN UINT64                                   DestAddress,
  IN UINT64                                   SrcAddress,
  IN UINTN                                    Count
  )
;

/**

  Allows read from PCI configuration space.

  @param This     -  A pointer to EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL
  @param Width    -  Signifies the width of the memory operation.
  @param Address  -  The address within the PCI configuration space
                     for the PCI controller.
  @param Count    -  The number of PCI configuration operations
                     to perform.
  @param Buffer   -  The destination buffer to store the results.

  @retval EFI_SUCCESS            -  The data was read from the PCI root bridge.
  @retval EFI_INVALID_PARAMETER  -  Invalid parameters found.
  @retval EFI_OUT_OF_RESOURCES   -  The request could not be completed due to a lack of
                            @retval resources.
**/
EFI_STATUS
EFIAPI
RootBridgeIoPciRead (
  IN     EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL        *This,
  IN     EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_WIDTH  Width,
  IN     UINT64                                 Address,
  IN     UINTN                                  Count,
  IN OUT VOID                                   *Buffer
  )
;

/**

  Allows write to PCI configuration space.

  @param This     -  A pointer to EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL
  @param Width    -  Signifies the width of the memory operation.
  @param Address  -  The address within the PCI configuration space
                     for the PCI controller.
  @param Count    -  The number of PCI configuration operations
                     to perform.
  @param Buffer   -  The source buffer to get the results.

  @retval EFI_SUCCESS            -  The data was written to the PCI root bridge.
  @retval EFI_INVALID_PARAMETER  -  Invalid parameters found.
  @retval EFI_OUT_OF_RESOURCES   -  The request could not be completed due to a lack of
                            @retval resources.
**/
EFI_STATUS
EFIAPI
RootBridgeIoPciWrite (
  IN     EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL        *This,
  IN     EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_WIDTH  Width,
  IN     UINT64                                 Address,
  IN     UINTN                                  Count,
  IN OUT VOID                                   *Buffer
  )
;

/**

  Provides the PCI controller-specific address needed to access
  system memory for DMA.

  @param This           -  A pointer to the EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL.
  @param Operation      -  Indicate if the bus master is going to read or write
                           to system memory.
  @param HostAddress    -  The system memory address to map on the PCI controller.
  @param NumberOfBytes  -  On input the number of bytes to map.
                           On output the number of bytes that were mapped.
  @param DeviceAddress  -  The resulting map address for the bus master PCI
                           controller to use to access the system memory's HostAddress.
  @param Mapping        -  The value to pass to Unmap() when the bus master DMA
                           operation is complete.

  @retval EFI_SUCCESS            -  Success.
  @retval EFI_INVALID_PARAMETER  -  Invalid parameters found.
  @retval EFI_UNSUPPORTED        -  The HostAddress cannot be mapped as a common
                            @retval buffer.
  @retval EFI_DEVICE_ERROR       -  The System hardware could not map the requested
                            @retval address.
  @retval EFI_OUT_OF_RESOURCES   -  The request could not be completed due to
                            @retval lack of resources.

**/
EFI_STATUS
EFIAPI
RootBridgeIoMap (
  IN     EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL            *This,
  IN     EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_OPERATION  Operation,
  IN     VOID                                       *HostAddress,
  IN OUT UINTN                                      *NumberOfBytes,
  OUT    EFI_PHYSICAL_ADDRESS                       *DeviceAddress,
  OUT    VOID                                       **Mapping
  )
;

/**

  Completes the Map() operation and releases any corresponding resources.

  @param This     -  Pointer to the EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL instance.
  Mapping  -  The value returned from Map() operation.

  @retval EFI_SUCCESS            -  The range was unmapped successfully.
  @retval EFI_INVALID_PARAMETER  -  Mapping is not a value that was returned
                            @retval by Map operation.
  @retval EFI_DEVICE_ERROR       -  The data was not committed to the target
                            @retval system memory.

**/
EFI_STATUS
EFIAPI
RootBridgeIoUnmap (
  IN  EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL  *This,
  IN  VOID                             *Mapping
  )
;

/**

  Allocates pages that are suitable for a common buffer mapping.

  @param This         -  Pointer to EFI_ROOT_BRIDGE_IO_PROTOCOL instance.
  @param Type         -  Not used and can be ignored.
  @param MemoryType   -  Type of memory to allocate.
  @param Pages        -  Number of pages to allocate.
  @param HostAddress  -  Pointer to store the base system memory address
                         of the allocated range.
  @param Attributes   -  Requested bit mask of attributes of the allocated
                         range.

  @retval EFI_SUCCESS            -  The requested memory range were allocated.
  @retval EFI_INVALID_PARAMETER  -  Invalid parameter found.
  @retval EFI_UNSUPPORTED        -  Attributes is unsupported.

**/
EFI_STATUS
EFIAPI
RootBridgeIoAllocateBuffer (
  IN  EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL  *This,
  IN  EFI_ALLOCATE_TYPE                Type,
  IN  EFI_MEMORY_TYPE                  MemoryType,
  IN  UINTN                            Pages,
  OUT VOID                             **HostAddress,
  IN  UINT64                           Attributes
  )
;

/**

  Free memory allocated in AllocateBuffer.

  @param This         -  Pointer to EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL
                         instance.
  @param Pages        -  Number of pages to free.
  @param HostAddress  -  The base system memory address of the
                         allocated range.

  @retval EFI_SUCCESS            -  Requested memory pages were freed.
  @retval EFI_INVALID_PARAMETER  -  Invalid parameter found.

**/
EFI_STATUS
EFIAPI
RootBridgeIoFreeBuffer (
  IN  EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL  *This,
  IN  UINTN                            Pages,
  OUT VOID                             *HostAddress
  )
;

/**

  Flushes all PCI posted write transactions from a PCI host
  bridge to system memory.

  @param This  - Pointer to EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL instance.

  @retval EFI_SUCCESS       -  PCI posted write transactions were flushed
                       @retval from PCI host bridge to system memory.
  @retval EFI_DEVICE_ERROR  -  Fail due to hardware error.

**/
EFI_STATUS
EFIAPI
RootBridgeIoFlush (
  IN  EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL  *This
  )
;

/**

  Get the attributes that a PCI root bridge supports and
  the attributes the PCI root bridge is currently using.

  @param This        -  Pointer to EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL
                        instance.
  @param Supports    -  A pointer to the mask of attributes that
                        this PCI root bridge supports.
  @param Attributes  -  A pointer to the mask of attributes that
                        this PCI root bridge is currently using.
  @retval EFI_SUCCESS            -  Success.
  @retval EFI_INVALID_PARAMETER  -  Invalid parameter found.

**/
EFI_STATUS
EFIAPI
RootBridgeIoGetAttributes (
  IN  EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL  *This,
  OUT UINT64                           *Supported,
  OUT UINT64                           *Attributes
  )
;

/**

  Sets the attributes for a resource range on a PCI root bridge.

  @param This            -  Pointer to EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL instance.
  @param Attributes      -  The mask of attributes to set.
  @param ResourceBase    -  Pointer to the base address of the resource range
                            to be modified by the attributes specified by Attributes.
  @param ResourceLength  -  Pointer to the length of the resource range to be modified.

  @retval EFI_SUCCESS            -  Success.
  @retval EFI_INVALID_PARAMETER  -  Invalid parameter found.
  @retval EFI_OUT_OF_RESOURCES   -  Not enough resources to set the attributes upon.

**/
EFI_STATUS
EFIAPI
RootBridgeIoSetAttributes (
  IN     EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL  *This,
  IN     UINT64                           Attributes,
  IN OUT UINT64                           *ResourceBase,
  IN OUT UINT64                           *ResourceLength
  )
;

/**

  Retrieves the current resource settings of this PCI root bridge
  in the form of a set of ACPI 2.0 resource descriptor.

  @param This       -  Pointer to the EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL instance.
  @param Resources  -  Pointer to the ACPI 2.0 resource descriptor that
                       describe the current configuration of this PCI root
                       bridge.

  @retval EFI_SUCCESS      -  Success.
  @retval EFI_UNSUPPORTED  -  Current configuration of the PCI root bridge
                      @retval could not be retrieved.

**/
EFI_STATUS
EFIAPI
RootBridgeIoConfiguration (
  IN  EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL  *This,
  OUT VOID                             **Resources
  )
;


extern EFI_METRONOME_ARCH_PROTOCOL *mMetronome;
extern EFI_CPU_IO2_PROTOCOL         *mCpuIo;
#endif
