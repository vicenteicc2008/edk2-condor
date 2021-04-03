/** @file
  File to contain all the hardware specific stuff for the Smm Gpi dispatch protocol.

  Copyright (c) 2019 Intel Corporation. All rights reserved. <BR>

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "PchSmm.h"
#include "PchSmmHelpers.h"
#include <Library/SmiHandlerProfileLib.h>
#include <Register/PchRegsGpio.h>
#include <Register/PchRegsPmc.h>

//
// Structure for GPI SMI is a template which needs to have
// GPI Smi bit offset and Smi Status & Enable registers updated (accordingly
// to choosen group and pad number) after adding it to SMM Callback database
//

GLOBAL_REMOVE_IF_UNREFERENCED CONST PCH_SMM_SOURCE_DESC mPchGpiSourceDescTemplate = {
  PCH_SMM_NO_FLAGS,
  {
    NULL_BIT_DESC_INITIALIZER,
    NULL_BIT_DESC_INITIALIZER
  },
  {
    {
      {
        GPIO_ADDR_TYPE, {0x0}
      },
      S_GPIO_PCR_GP_SMI_STS, 0x0,
    }
  },
  {
    {
      ACPI_ADDR_TYPE,
      {R_ACPI_IO_SMI_STS}
    },
    S_ACPI_IO_SMI_STS,
    N_ACPI_IO_SMI_STS_GPIO_SMI
  }
};

/**
  The register function used to register SMI handler of GPI SMI event.

  @param[in]  This               Pointer to the EFI_SMM_GPI_DISPATCH2_PROTOCOL instance.
  @param[in]  DispatchFunction   Function to register for handler when the specified GPI causes an SMI.
  @param[in]  RegisterContext    Pointer to the dispatch function's context.
                                 The caller fills this context in before calling
                                 the register function to indicate to the register
                                 function the GPI(s) for which the dispatch function
                                 should be invoked.
  @param[out] DispatchHandle     Handle generated by the dispatcher to track the
                                 function instance.

  @retval EFI_SUCCESS            The dispatch function has been successfully
                                 registered and the SMI source has been enabled.
  @retval EFI_ACCESS_DENIED      Register is not allowed
  @retval EFI_INVALID_PARAMETER  RegisterContext is invalid. The GPI input value
                                 is not within valid range.
  @retval EFI_OUT_OF_RESOURCES   There is not enough memory (system or SMM) to manage this child.
**/
EFI_STATUS
EFIAPI
PchGpiSmiRegister (
  IN CONST EFI_SMM_GPI_DISPATCH2_PROTOCOL  *This,
  IN       EFI_SMM_HANDLER_ENTRY_POINT2    DispatchFunction,
  IN CONST EFI_SMM_GPI_REGISTER_CONTEXT    *RegisterContext,
  OUT      EFI_HANDLE                      *DispatchHandle
  )
{
  EFI_STATUS                  Status;
  DATABASE_RECORD             Record;
  GPIO_PAD                    GpioPad;
  UINT8                       GpiSmiBitOffset;
  UINT32                      GpiHostSwOwnRegAddress;
  UINT32                      GpiSmiStsRegAddress;
  UINT32                      Data32Or;
  UINT32                      Data32And;

  //
  // Return access denied if the SmmReadyToLock event has been triggered
  //
  if (mReadyToLock == TRUE) {
    DEBUG ((DEBUG_ERROR, "Register is not allowed if the EndOfDxe event has been triggered! \n"));
    return EFI_ACCESS_DENIED;
  }

  Status = GpioGetPadAndSmiRegs (
             (UINT32) RegisterContext->GpiNum,
             &GpioPad,
             &GpiSmiBitOffset,
             &GpiHostSwOwnRegAddress,
             &GpiSmiStsRegAddress
             );

  if (EFI_ERROR (Status)) {
    return Status;
  }

  ZeroMem (&Record, sizeof (DATABASE_RECORD));

  //
  // Gather information about the registration request
  //
  Record.Callback          = DispatchFunction;
  Record.ChildContext.Gpi  = *RegisterContext;
  Record.ProtocolType      = GpiType;
  Record.Signature         = DATABASE_RECORD_SIGNATURE;

  CopyMem (&Record.SrcDesc, &mPchGpiSourceDescTemplate, sizeof (PCH_SMM_SOURCE_DESC) );

  Record.SrcDesc.Sts[0].Reg.Data.raw = GpiSmiStsRegAddress;  // GPI SMI Status register
  Record.SrcDesc.Sts[0].Bit = GpiSmiBitOffset;               // Bit position for selected pad

  //
  // Insert GpiSmi handler to PchSmmCore database
  //
  *DispatchHandle = NULL;

  Status = SmmCoreInsertRecord (
             &Record,
             DispatchHandle
             );
  ASSERT_EFI_ERROR (Status);

  SmiHandlerProfileRegisterHandler (&gEfiSmmGpiDispatch2ProtocolGuid, (EFI_SMM_HANDLER_ENTRY_POINT2) DispatchFunction, (UINTN)RETURN_ADDRESS (0), (void *)RegisterContext, sizeof(*RegisterContext));

  //
  // Enable GPI SMI
  // HOSTSW_OWN with respect to generating GPI SMI has negative logic:
  //  - 0 (ACPI mode) - GPIO pad will be capable of generating SMI/NMI/SCI
  //  - 1 (GPIO mode) - GPIO pad will not generate SMI/NMI/SCI
  //
  Data32And  = (UINT32)~(1u << GpiSmiBitOffset);
  MmioAnd32 (GpiHostSwOwnRegAddress, Data32And);

  //
  // Add HOSTSW_OWN programming into S3 boot script
  //
  Data32Or = 0;
  S3BootScriptSaveMemReadWrite (S3BootScriptWidthUint32, GpiHostSwOwnRegAddress, &Data32Or, &Data32And);

  return EFI_SUCCESS;
}

/**
  Unregister a GPI SMI source dispatch function with a parent SMM driver

  @param[in] This                 Pointer to the EFI_SMM_GPI_DISPATCH2_PROTOCOL instance.
  @param[in] DispatchHandle       Handle of dispatch function to deregister.

  @retval EFI_SUCCESS             The dispatch function has been successfully
                                  unregistered and the SMI source has been disabled
                                  if there are no other registered child dispatch
                                  functions for this SMI source.
  @retval EFI_INVALID_PARAMETER   Handle is invalid.
**/
EFI_STATUS
EFIAPI
PchGpiSmiUnRegister (
  IN CONST EFI_SMM_GPI_DISPATCH2_PROTOCOL  *This,
  IN       EFI_HANDLE                      DispatchHandle
  )
{
  EFI_STATUS      Status;
  DATABASE_RECORD *RecordToDelete;
  DATABASE_RECORD *RecordInDb;
  LIST_ENTRY      *LinkInDb;
  GPIO_PAD        GpioPad;
  UINT8           GpiSmiBitOffset;
  UINT32          GpiHostSwOwnRegAddress;
  UINT32          GpiSmiStsRegAddress;
  UINT32          Data32Or;
  UINT32          Data32And;
  BOOLEAN         DisableGpiSmiSource;


  if (DispatchHandle == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  RecordToDelete = DATABASE_RECORD_FROM_LINK (DispatchHandle);
  if ((RecordToDelete->Signature != DATABASE_RECORD_SIGNATURE) ||
      (RecordToDelete->ProtocolType != GpiType)) {
    return EFI_INVALID_PARAMETER;
  }

  //
  // Return access denied if the SmmReadyToLock event has been triggered
  //
  if (mReadyToLock == TRUE) {
    DEBUG ((DEBUG_ERROR, "UnRegister is not allowed if the SmmReadyToLock event has been triggered! \n"));
    return EFI_ACCESS_DENIED;
  }

  DisableGpiSmiSource = TRUE;
  //
  // Loop through all sources in record linked list to see if any other GPI SMI
  // is installed on the same pin. If no then disable GPI SMI capability on this pad
  //
  LinkInDb = GetFirstNode (&mPrivateData.CallbackDataBase);
  while (!IsNull (&mPrivateData.CallbackDataBase, LinkInDb)) {
    RecordInDb = DATABASE_RECORD_FROM_LINK (LinkInDb);
    LinkInDb = GetNextNode (&mPrivateData.CallbackDataBase, &RecordInDb->Link);
    //
    // If this is the record to delete skip it
    //
    if (RecordInDb == RecordToDelete) {
      continue;
    }
    //
    // Check if record is GPI SMI type
    //
    if (RecordInDb->ProtocolType == GpiType) {
      //
      // Check if same GPIO pad is the source of this SMI
      //
      if (RecordInDb->ChildContext.Gpi.GpiNum == RecordToDelete->ChildContext.Gpi.GpiNum) {
        DisableGpiSmiSource = FALSE;
        break;
      }
    }
  }

  if (DisableGpiSmiSource) {
    GpioGetPadAndSmiRegs (
      (UINT32) RecordToDelete->ChildContext.Gpi.GpiNum,
      &GpioPad,
      &GpiSmiBitOffset,
      &GpiHostSwOwnRegAddress,
      &GpiSmiStsRegAddress
      );

    Data32Or = 1u << GpiSmiBitOffset;
    Data32And = 0xFFFFFFFF;
    MmioOr32 (GpiHostSwOwnRegAddress, Data32Or);
    S3BootScriptSaveMemReadWrite (S3BootScriptWidthUint32, GpiHostSwOwnRegAddress, &Data32Or, &Data32And);
  }


  RemoveEntryList (&RecordToDelete->Link);
  ZeroMem (RecordToDelete, sizeof (DATABASE_RECORD));
  Status = gSmst->SmmFreePool (RecordToDelete);

  if (EFI_ERROR (Status)) {
    ASSERT (FALSE);
    return Status;
  }
  SmiHandlerProfileUnregisterHandler (&gEfiSmmGpiDispatch2ProtocolGuid, RecordToDelete->Callback, NULL, 0);
  return EFI_SUCCESS;
}
