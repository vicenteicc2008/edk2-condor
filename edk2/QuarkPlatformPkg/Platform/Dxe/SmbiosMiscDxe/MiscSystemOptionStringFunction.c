/** @file
BIOS system option string boot time changes.
SMBIOS type 12.

Copyright (c) 2013-2015 Intel Corporation.

This program and the accompanying materials
are licensed and made available under the terms and conditions of the BSD License
which accompanies this distribution.  The full text of the license may be found at
http://opensource.org/licenses/bsd-license.php

THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.


**/


#include "CommonHeader.h"
#include "SmbiosMisc.h"


/**
  This function makes boot time changes to the contents of the
  MiscSystemOptionString (Type 12).

  @param  RecordData                 Pointer to copy of RecordData from the Data Table.

  @retval EFI_SUCCESS                All parameters were valid.
  @retval EFI_UNSUPPORTED            Unexpected RecordType value.
  @retval EFI_INVALID_PARAMETER      Invalid parameter was found.

**/
MISC_SMBIOS_TABLE_FUNCTION(SystemOptionString)
{
  CHAR8                             *OptionalStrStart;
  UINTN                             OptStrLen;
  EFI_STRING                        OptionString;
  EFI_STATUS                        Status;
  STRING_REF                        TokenToGet;
  EFI_SMBIOS_HANDLE                 SmbiosHandle;
  SMBIOS_TABLE_TYPE12               *SmbiosRecord;
  EFI_MISC_SYSTEM_OPTION_STRING     *ForType12InputData;

  ForType12InputData = (EFI_MISC_SYSTEM_OPTION_STRING *)RecordData;

  //
  // First check for invalid parameters.
  //
  if (RecordData == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  TokenToGet = STRING_TOKEN (STR_MISC_SYSTEM_OPTION_STRING);
  OptionString = HiiGetPackageString(&gEfiCallerIdGuid, TokenToGet, NULL);
  OptStrLen = StrLen(OptionString);
  if (OptStrLen > SMBIOS_STRING_MAX_LENGTH) {
    return EFI_UNSUPPORTED;
  }

  //
  // Two zeros following the last string.
  //
  SmbiosRecord = AllocatePool(sizeof (SMBIOS_TABLE_TYPE12) + OptStrLen + 1 + 1);
  ZeroMem(SmbiosRecord, sizeof (SMBIOS_TABLE_TYPE12) + OptStrLen + 1 + 1);

  SmbiosRecord->Hdr.Type = EFI_SMBIOS_TYPE_SYSTEM_CONFIGURATION_OPTIONS;
  SmbiosRecord->Hdr.Length = sizeof (SMBIOS_TABLE_TYPE12);
  //
  // Make handle chosen by smbios protocol.add automatically.
  //
  SmbiosRecord->Hdr.Handle = 0;

  SmbiosRecord->StringCount = 1;
  OptionalStrStart = (CHAR8*) (SmbiosRecord + 1);
  UnicodeStrToAsciiStr(OptionString, OptionalStrStart);
  //
  // Now we have got the full smbios record, call smbios protocol to add this record.
  //
  SmbiosHandle = SMBIOS_HANDLE_PI_RESERVED;
  Status = Smbios-> Add(
                      Smbios,
                      NULL,
                      &SmbiosHandle,
                      (EFI_SMBIOS_TABLE_HEADER *) SmbiosRecord
                      );

  FreePool(SmbiosRecord);
  return Status;
}
