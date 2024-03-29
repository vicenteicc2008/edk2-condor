/** @file

  The Header file of the Pci Host Bridge Driver.

Copyright (c) 1999 - 2016, Intel Corporation. All rights reserved.<BR>
This program and the accompanying materials
are licensed and made available under the terms and conditions of the BSD License
which accompanies this distribution.  The full text of the license may be found at
http://opensource.org/licenses/bsd-license.php

THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

**/

#ifndef _PCI_HOST_BRIDGE_H_
#define _PCI_HOST_BRIDGE_H_


#include <PiDxe.h>
#include <IndustryStandard/Acpi.h>
#include <Library/UefiDriverEntryPoint.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PciHostBridgeLib.h>
#include <Protocol/PciHostBridgeResourceAllocation.h>

#include "PciRootBridge.h"

#define PCI_HOST_BRIDGE_SIGNATURE SIGNATURE_32 ('p', 'h', 'b', 'g')
typedef struct {
  UINTN                                             Signature;
  EFI_HANDLE                                        Handle;
  LIST_ENTRY                                        RootBridges;
  BOOLEAN                                           CanRestarted;
  EFI_PCI_HOST_BRIDGE_RESOURCE_ALLOCATION_PROTOCOL  ResAlloc;
} PCI_HOST_BRIDGE_INSTANCE;

#define PCI_HOST_BRIDGE_FROM_THIS(a) CR (a, PCI_HOST_BRIDGE_INSTANCE, ResAlloc, PCI_HOST_BRIDGE_SIGNATURE)

//
// Driver Entry Point
//
/**

  Entry point of this driver.

  @param ImageHandle  -  Image handle of this driver.
  @param SystemTable  -  Pointer to standard EFI system table.

  @retval EFI_SUCCESS       -  Succeed.
  @retval EFI_DEVICE_ERROR  -  Fail to install PCI_ROOT_BRIDGE_IO protocol.

**/
EFI_STATUS
EFIAPI
InitializePciHostBridge (
  IN EFI_HANDLE         ImageHandle,
  IN EFI_SYSTEM_TABLE   *SystemTable
  );

//
//  HostBridge Resource Allocation interface
//
/**

  Enter a certain phase of the PCI enumeration process.

  @param This   -  The EFI_PCI_HOST_BRIDGE_RESOURCE_ALLOCATION_PROTOCOL instance.
  @param Phase  -  The phase during enumeration.

  @retval EFI_SUCCESS            -  Succeed.
  @retval EFI_INVALID_PARAMETER  -  Wrong phase parameter passed in.
  @retval EFI_NOT_READY          -  Resources have not been submitted yet.

**/
EFI_STATUS
EFIAPI
NotifyPhase (
  IN EFI_PCI_HOST_BRIDGE_RESOURCE_ALLOCATION_PROTOCOL   *This,
  IN EFI_PCI_HOST_BRIDGE_RESOURCE_ALLOCATION_PHASE      Phase
  );

/**

  Return the device handle of the next PCI root bridge that is associated with
  this Host Bridge.

  @param This              - The EFI_PCI_HOST_BRIDGE_RESOURCE_ALLOCATION_ PROTOCOL instance.
  RootBridgeHandle  -  Returns the device handle of the next PCI Root Bridge.
                       On input, it holds the RootBridgeHandle returned by the most
                       recent call to GetNextRootBridge().The handle for the first
                       PCI Root Bridge is returned if RootBridgeHandle is NULL on input.

  @retval EFI_SUCCESS            -  Succeed.
  @retval EFI_NOT_FOUND          -  Next PCI root bridge not found.
  @retval EFI_INVALID_PARAMETER  -  Wrong parameter passed in.

**/
EFI_STATUS
EFIAPI
GetNextRootBridge (
  IN     EFI_PCI_HOST_BRIDGE_RESOURCE_ALLOCATION_PROTOCOL *This,
  IN OUT EFI_HANDLE                                       *RootBridgeHandle
  );

/**

  Returns the attributes of a PCI Root Bridge.

  @param This              -  The EFI_PCI_HOST_BRIDGE_RESOURCE_ALLOCATION_ PROTOCOL instance
  @param RootBridgeHandle  -  The device handle of the PCI Root Bridge
                              that the caller is interested in
  @param Attributes        -  The pointer to attributes of the PCI Root Bridge

  @retval EFI_SUCCESS            -  Succeed.
  @retval EFI_INVALID_PARAMETER  -  Attributes parameter passed in is NULL or
                            @retval RootBridgeHandle is not an EFI_HANDLE
                            @retval that was returned on a previous call to
                            @retval GetNextRootBridge().

**/
EFI_STATUS
EFIAPI
GetAttributes (
  IN  EFI_PCI_HOST_BRIDGE_RESOURCE_ALLOCATION_PROTOCOL *This,
  IN  EFI_HANDLE                                       RootBridgeHandle,
  OUT UINT64                                           *Attributes
  );

/**

  This is the request from the PCI enumerator to set up
  the specified PCI Root Bridge for bus enumeration process.

  @param This              -  The EFI_PCI_HOST_BRIDGE_RESOURCE_ALLOCATION_ PROTOCOL instance.
  @param RootBridgeHandle  -  The PCI Root Bridge to be set up.
  @param Configuration     -  Pointer to the pointer to the PCI bus resource descriptor.

  @retval EFI_SUCCESS            -  Succeed.
  @retval EFI_OUT_OF_RESOURCES   -  Not enough pool to be allocated.
  @retval EFI_INVALID_PARAMETER  -  RootBridgeHandle is not a valid handle.

**/
EFI_STATUS
EFIAPI
StartBusEnumeration (
  IN  EFI_PCI_HOST_BRIDGE_RESOURCE_ALLOCATION_PROTOCOL *This,
  IN  EFI_HANDLE                                       RootBridgeHandle,
  OUT VOID                                             **Configuration
  );

/**

  This function programs the PCI Root Bridge hardware so that
  it decodes the specified PCI bus range.

  @param This              -  The EFI_PCI_HOST_BRIDGE_RESOURCE_ALLOCATION_ PROTOCOL instance.
  @param RootBridgeHandle  -  The PCI Root Bridge whose bus range is to be programmed.
  @param Configuration     -  The pointer to the PCI bus resource descriptor.

  @retval EFI_SUCCESS            -  Succeed.
  @retval EFI_INVALID_PARAMETER  -  Wrong parameters passed in.

**/
EFI_STATUS
EFIAPI
SetBusNumbers (
  IN EFI_PCI_HOST_BRIDGE_RESOURCE_ALLOCATION_PROTOCOL *This,
  IN EFI_HANDLE                                       RootBridgeHandle,
  IN VOID                                             *Configuration
  );

/**

  Submits the I/O and memory resource requirements for the specified PCI Root Bridge.

  @param This              -  The EFI_PCI_HOST_BRIDGE_RESOURCE_ALLOCATION_ PROTOCOL instance
  @param RootBridgeHandle  -  The PCI Root Bridge whose I/O and memory resource requirements
                              are being submitted
  @param Configuration     -  The pointer to the PCI I/O and PCI memory resource descriptor

  @retval EFI_SUCCESS            -  Succeed.
  @retval EFI_INVALID_PARAMETER  -  Wrong parameters passed in.

**/
EFI_STATUS
EFIAPI
SubmitResources (
  IN EFI_PCI_HOST_BRIDGE_RESOURCE_ALLOCATION_PROTOCOL *This,
  IN EFI_HANDLE                                       RootBridgeHandle,
  IN VOID                                             *Configuration
  );

/**

  This function returns the proposed resource settings for the specified
  PCI Root Bridge.

  @param This              -  The EFI_PCI_HOST_BRIDGE_RESOURCE_ALLOCATION_ PROTOCOL instance.
  @param RootBridgeHandle  -  The PCI Root Bridge handle.
  @param Configuration     -  The pointer to the pointer to the PCI I/O
                              and memory resource descriptor.

  @retval EFI_SUCCESS            -  Succeed.
  @retval EFI_OUT_OF_RESOURCES   -  Not enough pool to be allocated.
  @retval EFI_INVALID_PARAMETER  -  RootBridgeHandle is not a valid handle.

**/
EFI_STATUS
EFIAPI
GetProposedResources (
  IN  EFI_PCI_HOST_BRIDGE_RESOURCE_ALLOCATION_PROTOCOL *This,
  IN  EFI_HANDLE                                       RootBridgeHandle,
  OUT VOID                                             **Configuration
  );

/**

  This function is called for all the PCI controllers that the PCI
  bus driver finds. Can be used to Preprogram the controller.

  @param This              -  The EFI_PCI_HOST_BRIDGE_RESOURCE_ALLOCATION_ PROTOCOL instance.
  @param RootBridgeHandle  -  The PCI Root Bridge handle.
  @param PciAddress        -  Address of the controller on the PCI bus.
  @param Phase             -  The Phase during resource allocation.

  @retval EFI_SUCCESS            -  Succeed.
  @retval EFI_INVALID_PARAMETER  -  RootBridgeHandle is not a valid handle.

**/
EFI_STATUS
EFIAPI
PreprocessController (
  IN EFI_PCI_HOST_BRIDGE_RESOURCE_ALLOCATION_PROTOCOL          *This,
  IN EFI_HANDLE                                                RootBridgeHandle,
  IN EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_PCI_ADDRESS               PciAddress,
  IN EFI_PCI_CONTROLLER_RESOURCE_ALLOCATION_PHASE              Phase
  );

/**
  This routine constructs the resource descriptors for all root bridges and call PciHostBridgeResourceConflict().

  @param HostBridge The Host Bridge Instance where the resource adjustment happens.
**/
VOID
ResourceConflict (
  IN  PCI_HOST_BRIDGE_INSTANCE *HostBridge
  );

extern EFI_METRONOME_ARCH_PROTOCOL *mMetronome;
extern EFI_CPU_IO2_PROTOCOL        *mCpuIo;
#endif
