/** @file
  Implement TPM1.2 NV Self Test related commands.

Copyright (c) 2016, Intel Corporation. All rights reserved. <BR>
This program and the accompanying materials
are licensed and made available under the terms and conditions of the BSD License
which accompanies this distribution.  The full text of the license may be found at
http://opensource.org/licenses/bsd-license.php

THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

**/

#include <PiPei.h>
#include <Library/Tpm12CommandLib.h>
#include <Library/BaseLib.h>
#include <Library/Tpm12DeviceLib.h>

/**
Send TPM_ContinueSelfTest command to TPM.

@retval EFI_SUCCESS           Operation completed successfully.
@retval EFI_TIMEOUT           The register can't run into the expected status in time.
@retval EFI_BUFFER_TOO_SMALL  Response data buffer is too small.
@retval EFI_DEVICE_ERROR      Unexpected device behavior.

**/
EFI_STATUS
EFIAPI
Tpm12ContinueSelfTest (
  VOID
  )
{
  TPM_RQU_COMMAND_HDR  Command;
  TPM_RSP_COMMAND_HDR  Response;
  UINT32               Length;

  //
  // send Tpm command TPM_ORD_ContinueSelfTest
  //
  Command.tag       = SwapBytes16 (TPM_TAG_RQU_COMMAND);
  Command.paramSize = SwapBytes32 (sizeof (Command));
  Command.ordinal   = SwapBytes32 (TPM_ORD_ContinueSelfTest);
  return Tpm12SubmitCommand (sizeof (Command), (UINT8 *)&Command, &Length, (UINT8 *)&Response);
}
