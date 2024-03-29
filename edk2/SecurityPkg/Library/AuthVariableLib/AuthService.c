/** @file
  Implement authentication services for the authenticated variables.

  Caution: This module requires additional review when modified.
  This driver will have external input - variable data. It may be input in SMM mode.
  This external input must be validated carefully to avoid security issue like
  buffer overflow, integer overflow.
  Variable attribute should also be checked to avoid authentication bypass.
     The whole SMM authentication variable design relies on the integrity of flash part and SMM.
  which is assumed to be protected by platform.  All variable code and metadata in flash/SMM Memory
  may not be modified without authorization. If platform fails to protect these resources,
  the authentication service provided in this driver will be broken, and the behavior is undefined.

  ProcessVarWithPk(), ProcessVarWithKek() and ProcessVariable() are the function to do
  variable authentication.

  VerifyTimeBasedPayloadAndUpdate() and VerifyCounterBasedPayload() are sub function to do verification.
  They will do basic validation for authentication data structure, then call crypto library
  to verify the signature.

Copyright (c) 2009 - 2016, Intel Corporation. All rights reserved.<BR>
This program and the accompanying materials
are licensed and made available under the terms and conditions of the BSD License
which accompanies this distribution.  The full text of the license may be found at
http://opensource.org/licenses/bsd-license.php

THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

**/

#include "AuthServiceInternal.h"

//
// Public Exponent of RSA Key.
//
CONST UINT8 mRsaE[] = { 0x01, 0x00, 0x01 };

//
// Requirement for different signature type which have been defined in UEFI spec.
// These data are used to perform SignatureList format check while setting PK/KEK variable.
//
EFI_SIGNATURE_ITEM mSupportSigItem[] = {
//{SigType,                       SigHeaderSize,   SigDataSize  }
  {EFI_CERT_SHA256_GUID,          0,               32           },
  {EFI_CERT_RSA2048_GUID,         0,               256          },
  {EFI_CERT_RSA2048_SHA256_GUID,  0,               256          },
  {EFI_CERT_SHA1_GUID,            0,               20           },
  {EFI_CERT_RSA2048_SHA1_GUID,    0,               256          },
  {EFI_CERT_X509_GUID,            0,               ((UINT32) ~0)},
  {EFI_CERT_SHA224_GUID,          0,               28           },
  {EFI_CERT_SHA384_GUID,          0,               48           },
  {EFI_CERT_SHA512_GUID,          0,               64           },
  {EFI_CERT_X509_SHA256_GUID,     0,               48           },
  {EFI_CERT_X509_SHA384_GUID,     0,               64           },
  {EFI_CERT_X509_SHA512_GUID,     0,               80           }
};

//
// Secure Boot Mode state machine
//
SECURE_BOOT_MODE mSecureBootState[SecureBootModeTypeMax] = {
  // USER MODE
  {
      AUDIT_MODE_DISABLE,                        // AuditMode
      FALSE,                                     // IsAuditModeRO, AuditMode is RW
      DEPLOYED_MODE_DISABLE,                     // DeployedMode
      FALSE,                                     // IsDeployedModeRO, DeployedMode is RW
      SETUP_MODE_DISABLE,                        // SetupMode
                                                 // SetupMode is always RO
      SECURE_BOOT_MODE_ENABLE                    // SecureBoot
  },
  // SETUP MODE
  {
      AUDIT_MODE_DISABLE,                        // AuditMode
      FALSE,                                     // IsAuditModeRO, AuditMode is RW
      DEPLOYED_MODE_DISABLE,                     // DeployedMode
      TRUE,                                      // IsDeployedModeRO, DeployedMode is RO
      SETUP_MODE_ENABLE,                         // SetupMode
                                                 // SetupMode is always RO
      SECURE_BOOT_MODE_DISABLE                   // SecureBoot
  },
  // AUDIT MODE
  {
      AUDIT_MODE_ENABLE,                         // AuditMode
      TRUE,                                      // AuditModeValAttr RO, AuditMode is RO
      DEPLOYED_MODE_DISABLE,                     // DeployedMode
      TRUE,                                      // DeployedModeValAttr RO, DeployedMode is RO
      SETUP_MODE_ENABLE,                         // SetupMode
                                                 // SetupMode is always RO
      SECURE_BOOT_MODE_DISABLE                   // SecureBoot
  },
  // DEPLOYED MODE
  {
      AUDIT_MODE_DISABLE,                        // AuditMode, AuditMode is RO
      TRUE,                                      // AuditModeValAttr RO
      DEPLOYED_MODE_ENABLE,                      // DeployedMode
      TRUE,                                      // DeployedModeValAttr RO, DeployedMode is RO
      SETUP_MODE_DISABLE,                        // SetupMode
                                                 // SetupMode is always RO
      SECURE_BOOT_MODE_ENABLE                    // SecureBoot
  }
};

SECURE_BOOT_MODE_TYPE  mSecureBootMode;

/**
  Finds variable in storage blocks of volatile and non-volatile storage areas.

  This code finds variable in storage blocks of volatile and non-volatile storage areas.
  If VariableName is an empty string, then we just return the first
  qualified variable without comparing VariableName and VendorGuid.

  @param[in]  VariableName          Name of the variable to be found.
  @param[in]  VendorGuid            Variable vendor GUID to be found.
  @param[out] Data                  Pointer to data address.
  @param[out] DataSize              Pointer to data size.

  @retval EFI_INVALID_PARAMETER     If VariableName is not an empty string,
                                    while VendorGuid is NULL.
  @retval EFI_SUCCESS               Variable successfully found.
  @retval EFI_NOT_FOUND             Variable not found

**/
EFI_STATUS
AuthServiceInternalFindVariable (
  IN  CHAR16            *VariableName,
  IN  EFI_GUID          *VendorGuid,
  OUT VOID              **Data,
  OUT UINTN             *DataSize
  )
{
  EFI_STATUS            Status;
  AUTH_VARIABLE_INFO    AuthVariableInfo;

  ZeroMem (&AuthVariableInfo, sizeof (AuthVariableInfo));
  Status = mAuthVarLibContextIn->FindVariable (
           VariableName,
           VendorGuid,
           &AuthVariableInfo
           );
  *Data = AuthVariableInfo.Data;
  *DataSize = AuthVariableInfo.DataSize;
  return Status;
}

/**
  Update the variable region with Variable information.

  @param[in] VariableName           Name of variable.
  @param[in] VendorGuid             Guid of variable.
  @param[in] Data                   Data pointer.
  @param[in] DataSize               Size of Data.
  @param[in] Attributes             Attribute value of the variable.

  @retval EFI_SUCCESS               The update operation is success.
  @retval EFI_INVALID_PARAMETER     Invalid parameter.
  @retval EFI_WRITE_PROTECTED       Variable is write-protected.
  @retval EFI_OUT_OF_RESOURCES      There is not enough resource.

**/
EFI_STATUS
AuthServiceInternalUpdateVariable (
  IN CHAR16             *VariableName,
  IN EFI_GUID           *VendorGuid,
  IN VOID               *Data,
  IN UINTN              DataSize,
  IN UINT32             Attributes
  )
{
  AUTH_VARIABLE_INFO    AuthVariableInfo;

  ZeroMem (&AuthVariableInfo, sizeof (AuthVariableInfo));
  AuthVariableInfo.VariableName = VariableName;
  AuthVariableInfo.VendorGuid = VendorGuid;
  AuthVariableInfo.Data = Data;
  AuthVariableInfo.DataSize = DataSize;
  AuthVariableInfo.Attributes = Attributes;

  return mAuthVarLibContextIn->UpdateVariable (
           &AuthVariableInfo
           );
}

/**
  Update the variable region with Variable information.

  @param[in] VariableName           Name of variable.
  @param[in] VendorGuid             Guid of variable.
  @param[in] Data                   Data pointer.
  @param[in] DataSize               Size of Data.
  @param[in] Attributes             Attribute value of the variable.
  @param[in] KeyIndex               Index of associated public key.
  @param[in] MonotonicCount         Value of associated monotonic count.

  @retval EFI_SUCCESS               The update operation is success.
  @retval EFI_INVALID_PARAMETER     Invalid parameter.
  @retval EFI_WRITE_PROTECTED       Variable is write-protected.
  @retval EFI_OUT_OF_RESOURCES      There is not enough resource.

**/
EFI_STATUS
AuthServiceInternalUpdateVariableWithMonotonicCount (
  IN CHAR16             *VariableName,
  IN EFI_GUID           *VendorGuid,
  IN VOID               *Data,
  IN UINTN              DataSize,
  IN UINT32             Attributes,
  IN UINT32             KeyIndex,
  IN UINT64             MonotonicCount
  )
{
  AUTH_VARIABLE_INFO    AuthVariableInfo;

  ZeroMem (&AuthVariableInfo, sizeof (AuthVariableInfo));
  AuthVariableInfo.VariableName = VariableName;
  AuthVariableInfo.VendorGuid = VendorGuid;
  AuthVariableInfo.Data = Data;
  AuthVariableInfo.DataSize = DataSize;
  AuthVariableInfo.Attributes = Attributes;
  AuthVariableInfo.PubKeyIndex = KeyIndex;
  AuthVariableInfo.MonotonicCount = MonotonicCount;

  return mAuthVarLibContextIn->UpdateVariable (
           &AuthVariableInfo
           );
}

/**
  Update the variable region with Variable information.

  @param[in] VariableName           Name of variable.
  @param[in] VendorGuid             Guid of variable.
  @param[in] Data                   Data pointer.
  @param[in] DataSize               Size of Data.
  @param[in] Attributes             Attribute value of the variable.
  @param[in] TimeStamp              Value of associated TimeStamp.

  @retval EFI_SUCCESS               The update operation is success.
  @retval EFI_INVALID_PARAMETER     Invalid parameter.
  @retval EFI_WRITE_PROTECTED       Variable is write-protected.
  @retval EFI_OUT_OF_RESOURCES      There is not enough resource.

**/
EFI_STATUS
AuthServiceInternalUpdateVariableWithTimeStamp (
  IN CHAR16             *VariableName,
  IN EFI_GUID           *VendorGuid,
  IN VOID               *Data,
  IN UINTN              DataSize,
  IN UINT32             Attributes,
  IN EFI_TIME           *TimeStamp
  )
{
  EFI_STATUS            FindStatus;
  VOID                  *OrgData;
  UINTN                 OrgDataSize;
  AUTH_VARIABLE_INFO    AuthVariableInfo;

  FindStatus = AuthServiceInternalFindVariable (
                 VariableName,
                 VendorGuid,
                 &OrgData,
                 &OrgDataSize
                 );

  //
  // EFI_VARIABLE_APPEND_WRITE attribute only effects for existing variable
  //
  if (!EFI_ERROR (FindStatus) && ((Attributes & EFI_VARIABLE_APPEND_WRITE) != 0)) {
    if ((CompareGuid (VendorGuid, &gEfiImageSecurityDatabaseGuid) &&
        ((StrCmp (VariableName, EFI_IMAGE_SECURITY_DATABASE) == 0) || (StrCmp (VariableName, EFI_IMAGE_SECURITY_DATABASE1) == 0) ||
        (StrCmp (VariableName, EFI_IMAGE_SECURITY_DATABASE2) == 0))) ||
        (CompareGuid (VendorGuid, &gEfiGlobalVariableGuid) && (StrCmp (VariableName, EFI_KEY_EXCHANGE_KEY_NAME) == 0))) {
      //
      // For variables with formatted as EFI_SIGNATURE_LIST, the driver shall not perform an append of
      // EFI_SIGNATURE_DATA values that are already part of the existing variable value.
      //
      FilterSignatureList (
        OrgData,
        OrgDataSize,
        Data,
        &DataSize
        );
    }
  }

  ZeroMem (&AuthVariableInfo, sizeof (AuthVariableInfo));
  AuthVariableInfo.VariableName = VariableName;
  AuthVariableInfo.VendorGuid = VendorGuid;
  AuthVariableInfo.Data = Data;
  AuthVariableInfo.DataSize = DataSize;
  AuthVariableInfo.Attributes = Attributes;
  AuthVariableInfo.TimeStamp = TimeStamp;
  return mAuthVarLibContextIn->UpdateVariable (
           &AuthVariableInfo
           );
}

/**
  Initialize Secure Boot variables.

  @retval EFI_SUCCESS               The initialization operation is successful.
  @retval EFI_OUT_OF_RESOURCES      There is not enough resource.

**/
EFI_STATUS
InitSecureBootVariables (
  VOID
  )
{
  EFI_STATUS             Status;
  UINT8                  *Data;
  UINTN                  DataSize;
  UINT32                 SecureBoot;
  UINT8                  SecureBootEnable;
  SECURE_BOOT_MODE_TYPE  SecureBootMode;
  BOOLEAN                IsPkPresent;

  //
  // Find "PK" variable
  //
  Status = AuthServiceInternalFindVariable (EFI_PLATFORM_KEY_NAME, &gEfiGlobalVariableGuid, (VOID **) &Data, &DataSize);
  if (EFI_ERROR (Status)) {
    IsPkPresent = FALSE;
    DEBUG ((EFI_D_INFO, "Variable %s does not exist.\n", EFI_PLATFORM_KEY_NAME));
  } else {
    IsPkPresent = TRUE;
    DEBUG ((EFI_D_INFO, "Variable %s exists.\n", EFI_PLATFORM_KEY_NAME));
  }

  //
  // Init "SecureBootMode" variable.
  // Initial case
  //   SecureBootMode doesn't exist. Init it with PK state
  // 3 inconsistency cases need to sync
  //   1.1 Add PK     -> system break -> update SecureBootMode Var
  //   1.2 Delete PK  -> system break -> update SecureBootMode Var
  //   1.3 Set AuditMode ->Delete PK  -> system break -> Update SecureBootMode Var
  //
  Status = AuthServiceInternalFindVariable (EDKII_SECURE_BOOT_MODE_NAME, &gEdkiiSecureBootModeGuid, (VOID **)&Data, &DataSize);
  if (EFI_ERROR(Status)) {
    //
    // Variable driver Initial Case
    //
    if (IsPkPresent) {
      SecureBootMode = SecureBootModeTypeUserMode;
    } else {
      SecureBootMode = SecureBootModeTypeSetupMode;
    }
  } else {
    //
    // 3 inconsistency cases need to sync
    //
    SecureBootMode = (SECURE_BOOT_MODE_TYPE)*Data;
    ASSERT(SecureBootMode < SecureBootModeTypeMax);

    if (IsPkPresent) {
      //
      // 3.1 Add PK     -> system break -> update SecureBootMode Var
      //
      if (SecureBootMode == SecureBootModeTypeSetupMode) {
        SecureBootMode = SecureBootModeTypeUserMode;
      } else if (SecureBootMode == SecureBootModeTypeAuditMode) {
        SecureBootMode = SecureBootModeTypeDeployedMode;
      }
    } else {
      //
      // 3.2 Delete PK -> system break -> update SecureBootMode Var
      // 3.3 Set AuditMode ->Delete PK  -> system break -> Update SecureBootMode Var. Reinit to be SetupMode
      //
      if ((SecureBootMode == SecureBootModeTypeUserMode) || (SecureBootMode == SecureBootModeTypeDeployedMode)) {
        SecureBootMode = SecureBootModeTypeSetupMode;
      }
    }
  }

  if (EFI_ERROR(Status) || (SecureBootMode != (SECURE_BOOT_MODE_TYPE)*Data)) {
    //
    // Update SecureBootMode Var
    //
    Status = AuthServiceInternalUpdateVariable (
               EDKII_SECURE_BOOT_MODE_NAME,
               &gEdkiiSecureBootModeGuid,
               &SecureBootMode,
               sizeof (UINT8),
               EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS
               );
    if (EFI_ERROR(Status)) {
      return Status;
    }
  }

  //
  // Init "AuditMode"
  //
  Status = AuthServiceInternalUpdateVariable (
             EFI_AUDIT_MODE_NAME,
             &gEfiGlobalVariableGuid,
             &mSecureBootState[SecureBootMode].AuditMode,
             sizeof(UINT8),
             EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS
             );
  if (EFI_ERROR(Status)) {
    return Status;
  }

  //
  // Init "DeployedMode"
  //
  Status = AuthServiceInternalUpdateVariable (
             EFI_DEPLOYED_MODE_NAME,
             &gEfiGlobalVariableGuid,
             &mSecureBootState[SecureBootMode].DeployedMode,
             sizeof(UINT8),
             EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS
             );
  if (EFI_ERROR(Status)) {
    return Status;
  }

  //
  // Init "SetupMode"
  //
  Status = AuthServiceInternalUpdateVariable (
             EFI_SETUP_MODE_NAME,
             &gEfiGlobalVariableGuid,
             &mSecureBootState[SecureBootMode].SetupMode,
             sizeof(UINT8),
             EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS
             );
  if (EFI_ERROR(Status)) {
    return Status;
  }

  //
  // If "SecureBootEnable" variable exists, then update "SecureBoot" variable.
  // If "SecureBootEnable" variable is SECURE_BOOT_ENABLE and in User Mode or Deployed Mode, Set "SecureBoot" variable to SECURE_BOOT_MODE_ENABLE.
  // If "SecureBootEnable" variable is SECURE_BOOT_DISABLE, Set "SecureBoot" variable to SECURE_BOOT_MODE_DISABLE.
  //
  SecureBootEnable = SECURE_BOOT_DISABLE;
  Status = AuthServiceInternalFindVariable (EFI_SECURE_BOOT_ENABLE_NAME, &gEfiSecureBootEnableDisableGuid, (VOID **)&Data, &DataSize);
  if (!EFI_ERROR(Status)) {
    if (!IsPkPresent) {
      //
      // PK is cleared in runtime. "SecureBootMode" is not updated before reboot
      // Delete "SecureBootMode"
      //
      Status = AuthServiceInternalUpdateVariable (
                 EFI_SECURE_BOOT_ENABLE_NAME,
                 &gEfiSecureBootEnableDisableGuid,
                 &SecureBootEnable,
                 0,
                 EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS
                 );
    } else {
      SecureBootEnable = *Data;
    }
  } else if ((SecureBootMode == SecureBootModeTypeUserMode) || (SecureBootMode == SecureBootModeTypeDeployedMode)) {
    //
    // "SecureBootEnable" not exist, initialize it in User Mode or Deployed Mode.
    //
    SecureBootEnable = SECURE_BOOT_ENABLE;
    Status = AuthServiceInternalUpdateVariable (
               EFI_SECURE_BOOT_ENABLE_NAME,
               &gEfiSecureBootEnableDisableGuid,
               &SecureBootEnable,
               sizeof (UINT8),
               EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS
               );
    if (EFI_ERROR (Status)) {
      return Status;
    }
  }

  //
  // Create "SecureBoot" variable with BS+RT attribute set.
  //
  if ((SecureBootEnable == SECURE_BOOT_ENABLE) 
  && ((SecureBootMode == SecureBootModeTypeUserMode) || (SecureBootMode == SecureBootModeTypeDeployedMode))) {
    SecureBoot = SECURE_BOOT_MODE_ENABLE;
  } else {
    SecureBoot = SECURE_BOOT_MODE_DISABLE;
  }
  Status = AuthServiceInternalUpdateVariable (
             EFI_SECURE_BOOT_MODE_NAME,
             &gEfiGlobalVariableGuid,
             &SecureBoot,
             sizeof (UINT8),
             EFI_VARIABLE_RUNTIME_ACCESS | EFI_VARIABLE_BOOTSERVICE_ACCESS
             );

  DEBUG ((EFI_D_INFO, "SecureBootMode is %x\n", SecureBootMode));
  DEBUG ((EFI_D_INFO, "Variable %s is %x\n", EFI_SECURE_BOOT_MODE_NAME, SecureBoot));
  DEBUG ((EFI_D_INFO, "Variable %s is %x\n", EFI_SECURE_BOOT_ENABLE_NAME, SecureBootEnable));

  //
  // Save SecureBootMode in global space
  //
  mSecureBootMode = SecureBootMode;

  return Status;
}

/**
  Update SecureBootMode variable.

  @param[in] NewMode                New Secure Boot Mode.

  @retval EFI_SUCCESS               The initialization operation is successful.
  @retval EFI_OUT_OF_RESOURCES      There is not enough resource.

**/
EFI_STATUS
UpdateSecureBootMode(
  IN  SECURE_BOOT_MODE_TYPE  NewMode
  )
{
  EFI_STATUS              Status;

  //
  // Update "SecureBootMode" variable to new Secure Boot Mode
  //
  Status = AuthServiceInternalUpdateVariable (
             EDKII_SECURE_BOOT_MODE_NAME,
             &gEdkiiSecureBootModeGuid,
             &NewMode,
             sizeof (UINT8),
             EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS
             );

  if (!EFI_ERROR(Status)) {
    DEBUG((EFI_D_INFO, "SecureBootMode Update to %x\n", NewMode));
    mSecureBootMode = NewMode;
  } else {
    DEBUG((EFI_D_ERROR, "SecureBootMode Update failure %x\n", Status));
  }

  return Status;
}

/**
  Current secure boot mode is AuditMode. This function performs secure boot mode transition
  to a new mode.

  @param[in] NewMode                New Secure Boot Mode.

  @retval EFI_SUCCESS               The initialization operation is successful.
  @retval EFI_OUT_OF_RESOURCES      There is not enough resource.

**/
EFI_STATUS
TransitionFromAuditMode(
  IN  SECURE_BOOT_MODE_TYPE               NewMode
  )
{
  EFI_STATUS  Status;
  VOID        *AuditVarData;
  VOID        *DeployedVarData;
  VOID        *SetupVarData;
  VOID        *SecureBootVarData;
  UINT8       SecureBootEnable;
  UINTN       DataSize;

  //
  // AuditMode/DeployedMode/SetupMode/SecureBoot are all NON_NV variable maintained by Variable driver
  // they can be RW. but can't be deleted. so they can always be found.
  //
  Status = AuthServiceInternalFindVariable (
             EFI_AUDIT_MODE_NAME,
             &gEfiGlobalVariableGuid,
             &AuditVarData,
             &DataSize
             );
  if (EFI_ERROR (Status)) {
    ASSERT(FALSE);
  }

  Status = AuthServiceInternalFindVariable (
             EFI_DEPLOYED_MODE_NAME,
             &gEfiGlobalVariableGuid,
             &DeployedVarData,
             &DataSize
             );
  if (EFI_ERROR (Status)) {
    ASSERT(FALSE);
  }

  Status = AuthServiceInternalFindVariable (
             EFI_SETUP_MODE_NAME,
             &gEfiGlobalVariableGuid,
             &SetupVarData,
             &DataSize
             );
  if (EFI_ERROR (Status)) {
    ASSERT(FALSE);
  }

  Status = AuthServiceInternalFindVariable (
             EFI_SECURE_BOOT_MODE_NAME,
             &gEfiGlobalVariableGuid,
             &SecureBootVarData,
             &DataSize
             );
  if (EFI_ERROR (Status)) {
    ASSERT(FALSE);
  }

  //
  // Make Secure Boot Mode transition ATOMIC
  // Update Private NV SecureBootMode Variable first, because it may fail due to NV range overflow.
  // other tranisition logic are all memory operations.
  //
  Status = UpdateSecureBootMode(NewMode);
  if (EFI_ERROR(Status)) {
    DEBUG((EFI_D_ERROR, "Update SecureBootMode Variable fail %x\n", Status));
  }

  if (NewMode == SecureBootModeTypeDeployedMode) {
    //
    // Since PK is enrolled, can't rollback, always update SecureBootMode in memory
    //
    mSecureBootMode = NewMode;
    Status          = EFI_SUCCESS;

    //
    // AuditMode ----> DeployedMode
    // Side Effects
    //   AuditMode =: 0 / DeployedMode := 1 / SetupMode := 0
    //
    // Update the value of AuditMode variable by a simple mem copy, this could avoid possible
    // variable storage reclaim at runtime.
    //
    CopyMem (AuditVarData, &mSecureBootState[NewMode].AuditMode, sizeof(UINT8));
    //
    // Update the value of DeployedMode variable by a simple mem copy, this could avoid possible
    // variable storage reclaim at runtime.
    //
    CopyMem (DeployedVarData, &mSecureBootState[NewMode].DeployedMode, sizeof(UINT8));
    //
    // Update the value of SetupMode variable by a simple mem copy, this could avoid possible
    // variable storage reclaim at runtime.
    //
    CopyMem (SetupVarData, &mSecureBootState[NewMode].SetupMode, sizeof(UINT8));

    if (mAuthVarLibContextIn->AtRuntime ()) {
      //
      // SecureBoot Variable indicates whether the platform firmware is operating
      // in Secure boot mode (1) or not (0), so we should not change SecureBoot
      // Variable in runtime.
      //
      return Status;
    }

    //
    // Update the value of SecureBoot variable by a simple mem copy, this could avoid possible
    // variable storage reclaim at runtime.
    //
    CopyMem (SecureBootVarData, &mSecureBootState[NewMode].SecureBoot, sizeof(UINT8));

    //
    // Create "SecureBootEnable" variable  as secure boot is enabled.
    //
    SecureBootEnable = SECURE_BOOT_ENABLE;
    AuthServiceInternalUpdateVariable (
      EFI_SECURE_BOOT_ENABLE_NAME,
      &gEfiSecureBootEnableDisableGuid,
      &SecureBootEnable,
      sizeof (SecureBootEnable),
      EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS
      );
  } else {
    DEBUG((EFI_D_ERROR, "Invalid state tranition from %x to %x\n", SecureBootModeTypeAuditMode, NewMode));
    ASSERT(FALSE);
  }

  return Status;
}

/**
  Current secure boot mode is DeployedMode. This function performs secure boot mode transition
  to a new mode.

  @param[in] NewMode                New Secure Boot Mode.

  @retval EFI_SUCCESS               The initialization operation is successful.
  @retval EFI_OUT_OF_RESOURCES      There is not enough resource.

**/
EFI_STATUS
TransitionFromDeployedMode(
  IN  SECURE_BOOT_MODE_TYPE               NewMode
  )
{
  EFI_STATUS  Status;
  VOID        *DeployedVarData;
  VOID        *SetupVarData;
  VOID        *SecureBootVarData;
  UINT8       SecureBootEnable;
  UINTN       DataSize;

  //
  // AuditMode/DeployedMode/SetupMode/SecureBoot are all NON_NV variable maintained by Variable driver
  // they can be RW. but can't be deleted. so they can always be found.
  //
  Status = AuthServiceInternalFindVariable (
             EFI_DEPLOYED_MODE_NAME,
             &gEfiGlobalVariableGuid,
             &DeployedVarData,
             &DataSize
             );
  if (EFI_ERROR (Status)) {
    ASSERT(FALSE);
  }

  Status = AuthServiceInternalFindVariable (
             EFI_SETUP_MODE_NAME,
             &gEfiGlobalVariableGuid,
             &SetupVarData,
             &DataSize
             );
  if (EFI_ERROR (Status)) {
    ASSERT(FALSE);
  }

  Status = AuthServiceInternalFindVariable (
             EFI_SECURE_BOOT_MODE_NAME,
             &gEfiGlobalVariableGuid,
             &SecureBootVarData,
             &DataSize
             );
  if (EFI_ERROR (Status)) {
    ASSERT(FALSE);
  }

  //
  // Make Secure Boot Mode transition ATOMIC
  // Update Private NV SecureBootMode Variable first, because it may fail due to NV range overflow.
  // other tranisition logic are all memory operations.
  //
  Status = UpdateSecureBootMode(NewMode);
  if (EFI_ERROR(Status)) {
    DEBUG((EFI_D_ERROR, "Update SecureBootMode Variable fail %x\n", Status));
  }

  switch(NewMode) {
    case SecureBootModeTypeUserMode:
      //
      // DeployedMode ----> UserMode
      // Side Effects
      //   DeployedMode := 0
      //
      // Platform Specific DeployedMode clear. UpdateSecureBootMode fails and no other variables are updated before. rollback this transition
      //
      if (EFI_ERROR(Status)) {
        return Status;
      }
      CopyMem (DeployedVarData, &mSecureBootState[NewMode].DeployedMode, sizeof(UINT8));

      break;

    case SecureBootModeTypeSetupMode:
      //
      // Since PK is processed before, can't rollback, still update SecureBootMode in memory
      //
      mSecureBootMode = NewMode;
      Status          = EFI_SUCCESS;

      //
      // DeployedMode ----> SetupMode
      //
      // Platform Specific PKpub clear or Delete Pkpub
      // Side Effects
      //   DeployedMode := 0 / SetupMode := 1 / SecureBoot := 0
      //
      // Update the value of DeployedMode variable by a simple mem copy, this could avoid possible
      // variable storage reclaim at runtime.
      //
      CopyMem (DeployedVarData, &mSecureBootState[NewMode].DeployedMode, sizeof(UINT8));
      //
      // Update the value of SetupMode variable by a simple mem copy, this could avoid possible
      // variable storage reclaim at runtime.
      //
      CopyMem (SetupVarData, &mSecureBootState[NewMode].SetupMode, sizeof(UINT8));

      if (mAuthVarLibContextIn->AtRuntime ()) {
        //
        // SecureBoot Variable indicates whether the platform firmware is operating
        // in Secure boot mode (1) or not (0), so we should not change SecureBoot
        // Variable in runtime.
        //
        return Status;
      }

      //
      // Update the value of SecureBoot variable by a simple mem copy, this could avoid possible
      // variable storage reclaim at runtime.
      //
      CopyMem (SecureBootVarData, &mSecureBootState[NewMode].SecureBoot, sizeof(UINT8));

      //
      // Delete the "SecureBootEnable" variable as secure boot is Disabled.
      //
      SecureBootEnable = SECURE_BOOT_DISABLE;
      AuthServiceInternalUpdateVariable (
        EFI_SECURE_BOOT_ENABLE_NAME,
        &gEfiSecureBootEnableDisableGuid,
        &SecureBootEnable,
        0,
        EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS
        );
      break;

    default:
      DEBUG((EFI_D_ERROR, "Invalid state tranition from %x to %x\n", SecureBootModeTypeDeployedMode, NewMode));
      ASSERT(FALSE);
  }

  return Status;
}

/**
  Current secure boot mode is UserMode. This function performs secure boot mode transition
  to a new mode.

  @param[in] NewMode                New Secure Boot Mode.

  @retval EFI_SUCCESS               The initialization operation is successful.
  @retval EFI_OUT_OF_RESOURCES      There is not enough resource.

**/
EFI_STATUS
TransitionFromUserMode(
  IN  SECURE_BOOT_MODE_TYPE               NewMode
  )
{
  EFI_STATUS   Status;
  VOID         *AuditVarData;
  VOID         *DeployedVarData;
  VOID         *SetupVarData;
  VOID         *PkVarData;
  VOID         *SecureBootVarData;
  UINT8        SecureBootEnable;
  UINTN        DataSize;
  VARIABLE_ENTRY_CONSISTENCY  VariableEntry;

  //
  // AuditMode/DeployedMode/SetupMode/SecureBoot are all NON_NV variable maintained by Variable driver
  // they can be RW. but can't be deleted. so they can always be found.
  //
  Status = AuthServiceInternalFindVariable (
             EFI_AUDIT_MODE_NAME,
             &gEfiGlobalVariableGuid,
             &AuditVarData,
             &DataSize
             );
  if (EFI_ERROR (Status)) {
    ASSERT(FALSE);
  }

  Status = AuthServiceInternalFindVariable (
             EFI_DEPLOYED_MODE_NAME,
             &gEfiGlobalVariableGuid,
             &DeployedVarData,
             &DataSize
             );
  if (EFI_ERROR (Status)) {
    ASSERT(FALSE);
  }

  Status = AuthServiceInternalFindVariable (
             EFI_SETUP_MODE_NAME,
             &gEfiGlobalVariableGuid,
             &SetupVarData,
             &DataSize
             );
  if (EFI_ERROR (Status)) {
    ASSERT(FALSE);
  }

  Status = AuthServiceInternalFindVariable (
             EFI_SECURE_BOOT_MODE_NAME,
             &gEfiGlobalVariableGuid,
             &SecureBootVarData,
             &DataSize
             );
  if (EFI_ERROR (Status)) {
    ASSERT(FALSE);
  }

  //
  // Make Secure Boot Mode transition ATOMIC
  // Update Private NV SecureBootMode Variable first, because it may fail due to NV range overflow. 
  // Other tranisition logic are all memory operations and PK delete is assumed to be always successful.
  //
  if (NewMode != SecureBootModeTypeAuditMode) {
    Status = UpdateSecureBootMode(NewMode);
    if (EFI_ERROR(Status)) {
      DEBUG((EFI_D_ERROR, "Update SecureBootMode Variable fail %x\n", Status));
    }
  } else {
    //
    // UserMode -----> AuditMode. Check RemainingSpace for SecureBootMode var first.
    // Will update SecureBootMode after DeletePK logic
    //
    VariableEntry.VariableSize = sizeof(UINT8);
    VariableEntry.Guid         = &gEdkiiSecureBootModeGuid;
    VariableEntry.Name         = EDKII_SECURE_BOOT_MODE_NAME;
    if (!mAuthVarLibContextIn->CheckRemainingSpaceForConsistency (VARIABLE_ATTRIBUTE_NV_BS_RT, &VariableEntry, NULL)) {
      return EFI_OUT_OF_RESOURCES;
    }
  }

  switch(NewMode) {
    case SecureBootModeTypeDeployedMode:
      //
      // UpdateSecureBootMode fails and no other variables are updated before. rollback this transition
      //
      if (EFI_ERROR(Status)) {
        return Status;
      }

      //
      // UserMode ----> DeployedMode
      // Side Effects
      //   DeployedMode := 1
      //
      CopyMem (DeployedVarData, &mSecureBootState[NewMode].DeployedMode, sizeof(UINT8));
      break;

    case SecureBootModeTypeAuditMode:
      //
      // UserMode ----> AuditMode
      // Side Effects
      //   Delete PKpub / SetupMode := 1 / SecureBoot := 0
      //
      // Delete PKpub without verification. Should always succeed.
      //
      PkVarData = NULL;
      Status = AuthServiceInternalUpdateVariable (
                 EFI_PLATFORM_KEY_NAME,
                 &gEfiGlobalVariableGuid,
                 PkVarData,
                 0,
                 EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS | EFI_VARIABLE_TIME_BASED_AUTHENTICATED_WRITE_ACCESS
                 );
      if (EFI_ERROR(Status)) {
        DEBUG((EFI_D_ERROR, "UserMode -> AuditMode. Delete PK fail %x\n", Status));
        ASSERT(FALSE);
      }

      //
      // Update Private NV SecureBootMode Variable
      //
      Status = UpdateSecureBootMode(NewMode);
      if (EFI_ERROR(Status)) {
        //
        // Since PK is deleted successfully, Doesn't break, continue to update other variable.
        //
        DEBUG((EFI_D_ERROR, "Update SecureBootMode Variable fail %x\n", Status));
      }
      CopyMem (AuditVarData, &mSecureBootState[NewMode].AuditMode, sizeof(UINT8));

      //
      // Fall into SetupMode logic
      //
    case SecureBootModeTypeSetupMode:
      //
      // Since PK is deleted before , can't rollback, still update SecureBootMode in memory
      //
      mSecureBootMode = NewMode;
      Status          = EFI_SUCCESS;

      //
      // UserMode ----> SetupMode
      //  Side Effects
      //    DeployedMode :=0 / SetupMode :=1 / SecureBoot :=0
      //
      // Update the value of SetupMode variable by a simple mem copy, this could avoid possible
      // variable storage reclaim at runtime.
      //
      CopyMem (SetupVarData, &mSecureBootState[NewMode].SetupMode, sizeof(UINT8));

      if (mAuthVarLibContextIn->AtRuntime ()) {
        //
        // SecureBoot Variable indicates whether the platform firmware is operating
        // in Secure boot mode (1) or not (0), so we should not change SecureBoot
        // Variable in runtime.
        //
        return Status;
      }

      //
      // Update the value of SecureBoot variable by a simple mem copy, this could avoid possible
      // variable storage reclaim at runtime.
      //
      CopyMem (SecureBootVarData, &mSecureBootState[NewMode].SecureBoot, sizeof(UINT8));

      //
      // Delete the "SecureBootEnable" variable as secure boot is Disabled.
      //
      SecureBootEnable = SECURE_BOOT_DISABLE;
      AuthServiceInternalUpdateVariable (
        EFI_SECURE_BOOT_ENABLE_NAME,
        &gEfiSecureBootEnableDisableGuid,
        &SecureBootEnable,
        0,
        EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS
        );

      break;

    default:
      DEBUG((EFI_D_ERROR, "Invalid state tranition from %x to %x\n", SecureBootModeTypeUserMode, NewMode));
      ASSERT(FALSE);
  }

  return Status;
}

/**
  Current secure boot mode is SetupMode. This function performs secure boot mode transition
  to a new mode.

  @param[in] NewMode                New Secure Boot Mode.

  @retval EFI_SUCCESS               The initialization operation is successful.
  @retval EFI_OUT_OF_RESOURCES      There is not enough resource.

**/
EFI_STATUS
TransitionFromSetupMode(
  IN  SECURE_BOOT_MODE_TYPE              NewMode
  )
{
  EFI_STATUS   Status;
  VOID         *AuditVarData;
  VOID         *SetupVarData;
  VOID         *SecureBootVarData;
  UINT8        SecureBootEnable;
  UINTN        DataSize;

  //
  // AuditMode/DeployedMode/SetupMode/SecureBoot are all NON_NV variable maintained by Variable driver
  // they can be RW. but can't be deleted. so they can always be found.
  //
  Status = AuthServiceInternalFindVariable (
             EFI_AUDIT_MODE_NAME,
             &gEfiGlobalVariableGuid,
             &AuditVarData,
             &DataSize
             );
  if (EFI_ERROR (Status)) {
    ASSERT(FALSE);
  }

  Status = AuthServiceInternalFindVariable (
             EFI_SETUP_MODE_NAME,
             &gEfiGlobalVariableGuid,
             &SetupVarData,
             &DataSize
             );
  if (EFI_ERROR (Status)) {
    ASSERT(FALSE);
  }

  Status = AuthServiceInternalFindVariable (
             EFI_SECURE_BOOT_MODE_NAME,
             &gEfiGlobalVariableGuid,
             &SecureBootVarData,
             &DataSize
             );
  if (EFI_ERROR (Status)) {
    ASSERT(FALSE);
  }

  //
  // Make Secure Boot Mode transition ATOMIC
  // Update Private NV SecureBootMode Variable first, because it may fail due to NV range overflow.
  // Other tranisition logic are all memory operations and PK delete is assumed to be always successful.
  //
  Status = UpdateSecureBootMode(NewMode);
  if (EFI_ERROR(Status)) {
    DEBUG((EFI_D_ERROR, "Update SecureBootMode Variable fail %x\n", Status));
  }

  switch(NewMode) {
    case SecureBootModeTypeAuditMode:
      //
      // UpdateSecureBootMode fails and no other variables are updated before. rollback this transition
      //
      if (EFI_ERROR(Status)) {
        return Status;
      }

      //
      // SetupMode ----> AuditMode
      // Side Effects
      //   AuditMode := 1
      //
      // Update the value of AuditMode variable by a simple mem copy, this could avoid possible
      // variable storage reclaim at runtime.
      //
      CopyMem (AuditVarData, &mSecureBootState[NewMode].AuditMode, sizeof(UINT8));
      break;

    case SecureBootModeTypeUserMode:
      //
      // Since PK is enrolled before, can't rollback, still update SecureBootMode in memory
      //
      mSecureBootMode = NewMode;
      Status          = EFI_SUCCESS;

      //
      // SetupMode ----> UserMode
      // Side Effects
      //   SetupMode := 0 / SecureBoot := 1
      //
      // Update the value of AuditMode variable by a simple mem copy, this could avoid possible
      // variable storage reclaim at runtime.
      //
      CopyMem (SetupVarData, &mSecureBootState[NewMode].SetupMode, sizeof(UINT8));

      if (mAuthVarLibContextIn->AtRuntime ()) {
        //
        // SecureBoot Variable indicates whether the platform firmware is operating
        // in Secure boot mode (1) or not (0), so we should not change SecureBoot
        // Variable in runtime.
        //
        return Status;
      }

      //
      // Update the value of SecureBoot variable by a simple mem copy, this could avoid possible
      // variable storage reclaim at runtime.
      //
      CopyMem (SecureBootVarData, &mSecureBootState[NewMode].SecureBoot, sizeof(UINT8));

      //
      // Create the "SecureBootEnable" variable as secure boot is enabled.
      //
      SecureBootEnable = SECURE_BOOT_ENABLE;
      AuthServiceInternalUpdateVariable (
        EFI_SECURE_BOOT_ENABLE_NAME,
        &gEfiSecureBootEnableDisableGuid,
        &SecureBootEnable,
        sizeof (SecureBootEnable),
        EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS
        );
      break;

    default:
      DEBUG((EFI_D_ERROR, "Invalid state tranition from %x to %x\n", SecureBootModeTypeSetupMode, NewMode));
      ASSERT(FALSE);
  }

  return Status;
}

/**
  This function performs main secure boot mode transition logic.

  @param[in] CurMode                Current Secure Boot Mode.
  @param[in] NewMode                New Secure Boot Mode.

  @retval EFI_SUCCESS               The initialization operation is successful.
  @retval EFI_OUT_OF_RESOURCES      There is not enough resource.
  @retval EFI_INVALID_PARAMETER     The Current Secure Boot Mode is wrong.

**/
EFI_STATUS
SecureBootModeTransition(
  IN  SECURE_BOOT_MODE_TYPE  CurMode,
  IN  SECURE_BOOT_MODE_TYPE  NewMode
  )
{
  EFI_STATUS Status;

  //
  // SecureBootMode transition
  //
  switch (CurMode) {
    case SecureBootModeTypeUserMode:
      Status = TransitionFromUserMode(NewMode);
      break;

    case SecureBootModeTypeSetupMode:
      Status = TransitionFromSetupMode(NewMode);
      break;

    case SecureBootModeTypeAuditMode:
      Status = TransitionFromAuditMode(NewMode);
      break;

    case SecureBootModeTypeDeployedMode:
      Status = TransitionFromDeployedMode(NewMode);
      break;

    default:
      Status = EFI_INVALID_PARAMETER;
      ASSERT(FALSE);
  }

  return Status;

}

/**
  Determine whether this operation needs a physical present user.

  @param[in]      VariableName            Name of the Variable.
  @param[in]      VendorGuid              GUID of the Variable.

  @retval TRUE      This variable is protected, only a physical present user could set this variable.
  @retval FALSE     This variable is not protected.

**/
BOOLEAN
NeedPhysicallyPresent(
  IN     CHAR16         *VariableName,
  IN     EFI_GUID       *VendorGuid
  )
{
  if ((CompareGuid (VendorGuid, &gEfiSecureBootEnableDisableGuid) && (StrCmp (VariableName, EFI_SECURE_BOOT_ENABLE_NAME) == 0))
    || (CompareGuid (VendorGuid, &gEfiCustomModeEnableGuid) && (StrCmp (VariableName, EFI_CUSTOM_MODE_NAME) == 0))) {
    return TRUE;
  }

  return FALSE;
}

/**
  Determine whether the platform is operating in Custom Secure Boot mode.

  @retval TRUE           The platform is operating in Custom mode.
  @retval FALSE          The platform is operating in Standard mode.

**/
BOOLEAN
InCustomMode (
  VOID
  )
{
  EFI_STATUS    Status;
  VOID          *Data;
  UINTN         DataSize;

  Status = AuthServiceInternalFindVariable (EFI_CUSTOM_MODE_NAME, &gEfiCustomModeEnableGuid, &Data, &DataSize);
  if (!EFI_ERROR (Status) && (*(UINT8 *) Data == CUSTOM_SECURE_BOOT_MODE)) {
    return TRUE;
  }

  return FALSE;
}

/**
  Get available public key index.

  @param[in] PubKey     Pointer to Public Key data.

  @return Public key index, 0 if no any public key index available.

**/
UINT32
GetAvailableKeyIndex (
  IN  UINT8             *PubKey
  )
{
  EFI_STATUS            Status;
  UINT8                 *Data;
  UINTN                 DataSize;
  UINT8                 *Ptr;
  UINT32                Index;
  BOOLEAN               IsFound;
  EFI_GUID              VendorGuid;
  CHAR16                Name[1];
  AUTH_VARIABLE_INFO    AuthVariableInfo;
  UINT32                KeyIndex;

  Status = AuthServiceInternalFindVariable (
             AUTHVAR_KEYDB_NAME,
             &gEfiAuthenticatedVariableGuid,
             (VOID **) &Data,
             &DataSize
             );
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "Get public key database variable failure, Status = %r\n", Status));
    return 0;
  }

  if (mPubKeyNumber == mMaxKeyNumber) {
    Name[0] = 0;
    AuthVariableInfo.VariableName = Name;
    ZeroMem (&VendorGuid, sizeof (VendorGuid));
    AuthVariableInfo.VendorGuid = &VendorGuid;
    mPubKeyNumber = 0;
    //
    // Collect valid key data.
    //
    do {
      Status = mAuthVarLibContextIn->FindNextVariable (AuthVariableInfo.VariableName, AuthVariableInfo.VendorGuid, &AuthVariableInfo);
      if (!EFI_ERROR (Status)) {
        if (AuthVariableInfo.PubKeyIndex != 0) {
          for (Ptr = Data; Ptr < (Data + DataSize); Ptr += sizeof (AUTHVAR_KEY_DB_DATA)) {
            if (ReadUnaligned32 (&(((AUTHVAR_KEY_DB_DATA *) Ptr)->KeyIndex)) == AuthVariableInfo.PubKeyIndex) {
              //
              // Check if the key data has been collected.
              //
              for (Index = 0; Index < mPubKeyNumber; Index++) {
                if (ReadUnaligned32 (&(((AUTHVAR_KEY_DB_DATA *) mPubKeyStore + Index)->KeyIndex)) == AuthVariableInfo.PubKeyIndex) {
                  break;
                }
              }
              if (Index == mPubKeyNumber) {
                //
                // New key data.
                //
                CopyMem ((AUTHVAR_KEY_DB_DATA *) mPubKeyStore + mPubKeyNumber, Ptr, sizeof (AUTHVAR_KEY_DB_DATA));
                mPubKeyNumber++;
              }
              break;
            }
          }
        }
      }
    } while (Status != EFI_NOT_FOUND);

    //
    // No available space to add new public key.
    //
    if (mPubKeyNumber == mMaxKeyNumber) {
      return 0;
    }
  }

  //
  // Find available public key index.
  //
  for (KeyIndex = 1; KeyIndex <= mMaxKeyNumber; KeyIndex++) {
    IsFound = FALSE;
    for (Ptr = mPubKeyStore; Ptr < (mPubKeyStore + mPubKeyNumber * sizeof (AUTHVAR_KEY_DB_DATA)); Ptr += sizeof (AUTHVAR_KEY_DB_DATA)) {
      if (ReadUnaligned32 (&(((AUTHVAR_KEY_DB_DATA *) Ptr)->KeyIndex)) == KeyIndex) {
        IsFound = TRUE;
        break;
      }
    }
    if (!IsFound) {
      break;
    }
  }

  return KeyIndex;
}

/**
  Add public key in store and return its index.

  @param[in] PubKey             Input pointer to Public Key data.
  @param[in] VariableDataEntry  The variable data entry.

  @return Index of new added public key.

**/
UINT32
AddPubKeyInStore (
  IN  UINT8                        *PubKey,
  IN  VARIABLE_ENTRY_CONSISTENCY   *VariableDataEntry
  )
{
  EFI_STATUS                       Status;
  UINT32                           Index;
  VARIABLE_ENTRY_CONSISTENCY       PublicKeyEntry;
  UINT32                           Attributes;
  UINT32                           KeyIndex;

  if (PubKey == NULL) {
    return 0;
  }

  //
  // Check whether the public key entry does exist.
  //
  for (Index = 0; Index < mPubKeyNumber; Index++) {
    if (CompareMem (((AUTHVAR_KEY_DB_DATA *) mPubKeyStore + Index)->KeyData, PubKey, EFI_CERT_TYPE_RSA2048_SIZE) == 0) {
      return ReadUnaligned32 (&(((AUTHVAR_KEY_DB_DATA *) mPubKeyStore + Index)->KeyIndex));
    }
  }

  KeyIndex = GetAvailableKeyIndex (PubKey);
  if (KeyIndex == 0) {
    return 0;
  }

  //
  // Check the variable space for both public key and variable data.
  //
  PublicKeyEntry.VariableSize = (mPubKeyNumber + 1) * sizeof (AUTHVAR_KEY_DB_DATA);
  PublicKeyEntry.Guid         = &gEfiAuthenticatedVariableGuid;
  PublicKeyEntry.Name         = AUTHVAR_KEYDB_NAME;
  Attributes = VARIABLE_ATTRIBUTE_NV_BS_RT | EFI_VARIABLE_AUTHENTICATED_WRITE_ACCESS;

  if (!mAuthVarLibContextIn->CheckRemainingSpaceForConsistency (Attributes, &PublicKeyEntry, VariableDataEntry, NULL)) {
    //
    // No enough variable space.
    //
    return 0;
  }

  WriteUnaligned32 (&(((AUTHVAR_KEY_DB_DATA *) mPubKeyStore + mPubKeyNumber)->KeyIndex), KeyIndex);
  CopyMem (((AUTHVAR_KEY_DB_DATA *) mPubKeyStore + mPubKeyNumber)->KeyData, PubKey, EFI_CERT_TYPE_RSA2048_SIZE);
  mPubKeyNumber++;

  //
  // Update public key database variable.
  //
  Status = AuthServiceInternalUpdateVariable (
             AUTHVAR_KEYDB_NAME,
             &gEfiAuthenticatedVariableGuid,
             mPubKeyStore,
             mPubKeyNumber * sizeof (AUTHVAR_KEY_DB_DATA),
             Attributes
             );
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "Update public key database variable failure, Status = %r\n", Status));
    return 0;
  }

  return KeyIndex;
}

/**
  Verify data payload with AuthInfo in EFI_CERT_TYPE_RSA2048_SHA256_GUID type.
  Follow the steps in UEFI2.2.

  Caution: This function may receive untrusted input.
  This function may be invoked in SMM mode, and datasize and data are external input.
  This function will do basic validation, before parse the data.
  This function will parse the authentication carefully to avoid security issues, like
  buffer overflow, integer overflow.

  @param[in]      Data                    Pointer to data with AuthInfo.
  @param[in]      DataSize                Size of Data.
  @param[in]      PubKey                  Public key used for verification.

  @retval EFI_INVALID_PARAMETER       Invalid parameter.
  @retval EFI_SECURITY_VIOLATION      If authentication failed.
  @retval EFI_SUCCESS                 Authentication successful.

**/
EFI_STATUS
VerifyCounterBasedPayload (
  IN     UINT8          *Data,
  IN     UINTN          DataSize,
  IN     UINT8          *PubKey
  )
{
  BOOLEAN                         Status;
  EFI_VARIABLE_AUTHENTICATION     *CertData;
  EFI_CERT_BLOCK_RSA_2048_SHA256  *CertBlock;
  UINT8                           Digest[SHA256_DIGEST_SIZE];
  VOID                            *Rsa;
  UINTN                           PayloadSize;

  PayloadSize = DataSize - AUTHINFO_SIZE;
  Rsa         = NULL;
  CertData    = NULL;
  CertBlock   = NULL;

  if (Data == NULL || PubKey == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  CertData  = (EFI_VARIABLE_AUTHENTICATION *) Data;
  CertBlock = (EFI_CERT_BLOCK_RSA_2048_SHA256 *) (CertData->AuthInfo.CertData);

  //
  // wCertificateType should be WIN_CERT_TYPE_EFI_GUID.
  // Cert type should be EFI_CERT_TYPE_RSA2048_SHA256_GUID.
  //
  if ((CertData->AuthInfo.Hdr.wCertificateType != WIN_CERT_TYPE_EFI_GUID) ||
      !CompareGuid (&CertData->AuthInfo.CertType, &gEfiCertTypeRsa2048Sha256Guid)) {
    //
    // Invalid AuthInfo type, return EFI_SECURITY_VIOLATION.
    //
    return EFI_SECURITY_VIOLATION;
  }
  //
  // Hash data payload with SHA256.
  //
  ZeroMem (Digest, SHA256_DIGEST_SIZE);
  Status  = Sha256Init (mHashCtx);
  if (!Status) {
    goto Done;
  }
  Status  = Sha256Update (mHashCtx, Data + AUTHINFO_SIZE, PayloadSize);
  if (!Status) {
    goto Done;
  }
  //
  // Hash Size.
  //
  Status  = Sha256Update (mHashCtx, &PayloadSize, sizeof (UINTN));
  if (!Status) {
    goto Done;
  }
  //
  // Hash Monotonic Count.
  //
  Status  = Sha256Update (mHashCtx, &CertData->MonotonicCount, sizeof (UINT64));
  if (!Status) {
    goto Done;
  }
  Status  = Sha256Final (mHashCtx, Digest);
  if (!Status) {
    goto Done;
  }
  //
  // Generate & Initialize RSA Context.
  //
  Rsa = RsaNew ();
  ASSERT (Rsa != NULL);
  //
  // Set RSA Key Components.
  // NOTE: Only N and E are needed to be set as RSA public key for signature verification.
  //
  Status = RsaSetKey (Rsa, RsaKeyN, PubKey, EFI_CERT_TYPE_RSA2048_SIZE);
  if (!Status) {
    goto Done;
  }
  Status = RsaSetKey (Rsa, RsaKeyE, mRsaE, sizeof (mRsaE));
  if (!Status) {
    goto Done;
  }
  //
  // Verify the signature.
  //
  Status = RsaPkcs1Verify (
             Rsa,
             Digest,
             SHA256_DIGEST_SIZE,
             CertBlock->Signature,
             EFI_CERT_TYPE_RSA2048_SHA256_SIZE
             );

Done:
  if (Rsa != NULL) {
    RsaFree (Rsa);
  }
  if (Status) {
    return EFI_SUCCESS;
  } else {
    return EFI_SECURITY_VIOLATION;
  }
}


/**
  Check input data form to make sure it is a valid EFI_SIGNATURE_LIST for PK/KEK/db/dbx/dbt variable.

  @param[in]  VariableName                Name of Variable to be check.
  @param[in]  VendorGuid                  Variable vendor GUID.
  @param[in]  Data                        Point to the variable data to be checked.
  @param[in]  DataSize                    Size of Data.

  @return EFI_INVALID_PARAMETER           Invalid signature list format.
  @return EFI_SUCCESS                     Passed signature list format check successfully.

**/
EFI_STATUS
CheckSignatureListFormat(
  IN  CHAR16                    *VariableName,
  IN  EFI_GUID                  *VendorGuid,
  IN  VOID                      *Data,
  IN  UINTN                     DataSize
  )
{
  EFI_SIGNATURE_LIST     *SigList;
  UINTN                  SigDataSize;
  UINT32                 Index;
  UINT32                 SigCount;
  BOOLEAN                IsPk;
  VOID                   *RsaContext;
  EFI_SIGNATURE_DATA     *CertData;
  UINTN                  CertLen;

  if (DataSize == 0) {
    return EFI_SUCCESS;
  }

  ASSERT (VariableName != NULL && VendorGuid != NULL && Data != NULL);

  if (CompareGuid (VendorGuid, &gEfiGlobalVariableGuid) && (StrCmp (VariableName, EFI_PLATFORM_KEY_NAME) == 0)){
    IsPk = TRUE;
  } else if ((CompareGuid (VendorGuid, &gEfiGlobalVariableGuid) && (StrCmp (VariableName, EFI_KEY_EXCHANGE_KEY_NAME) == 0)) ||
             (CompareGuid (VendorGuid, &gEfiImageSecurityDatabaseGuid) &&
             ((StrCmp (VariableName, EFI_IMAGE_SECURITY_DATABASE) == 0) || (StrCmp (VariableName, EFI_IMAGE_SECURITY_DATABASE1) == 0) ||
              (StrCmp (VariableName, EFI_IMAGE_SECURITY_DATABASE2) == 0)))) {
    IsPk = FALSE;
  } else {
    return EFI_SUCCESS;
  }

  SigCount = 0;
  SigList  = (EFI_SIGNATURE_LIST *) Data;
  SigDataSize  = DataSize;
  RsaContext = NULL;

  //
  // Walk throuth the input signature list and check the data format.
  // If any signature is incorrectly formed, the whole check will fail.
  //
  while ((SigDataSize > 0) && (SigDataSize >= SigList->SignatureListSize)) {
    for (Index = 0; Index < (sizeof (mSupportSigItem) / sizeof (EFI_SIGNATURE_ITEM)); Index++ ) {
      if (CompareGuid (&SigList->SignatureType, &mSupportSigItem[Index].SigType)) {
        //
        // The value of SignatureSize should always be 16 (size of SignatureOwner
        // component) add the data length according to signature type.
        //
        if (mSupportSigItem[Index].SigDataSize != ((UINT32) ~0) &&
          (SigList->SignatureSize - sizeof (EFI_GUID)) != mSupportSigItem[Index].SigDataSize) {
          return EFI_INVALID_PARAMETER;
        }
        if (mSupportSigItem[Index].SigHeaderSize != ((UINT32) ~0) &&
          SigList->SignatureHeaderSize != mSupportSigItem[Index].SigHeaderSize) {
          return EFI_INVALID_PARAMETER;
        }
        break;
      }
    }

    if (Index == (sizeof (mSupportSigItem) / sizeof (EFI_SIGNATURE_ITEM))) {
      //
      // Undefined signature type.
      //
      return EFI_INVALID_PARAMETER;
    }

    if (CompareGuid (&SigList->SignatureType, &gEfiCertX509Guid)) {
      //
      // Try to retrieve the RSA public key from the X.509 certificate.
      // If this operation fails, it's not a valid certificate.
      //
      RsaContext = RsaNew ();
      if (RsaContext == NULL) {
        return EFI_INVALID_PARAMETER;
      }
      CertData = (EFI_SIGNATURE_DATA *) ((UINT8 *) SigList + sizeof (EFI_SIGNATURE_LIST) + SigList->SignatureHeaderSize);
      CertLen = SigList->SignatureSize - sizeof (EFI_GUID);
      if (!RsaGetPublicKeyFromX509 (CertData->SignatureData, CertLen, &RsaContext)) {
        RsaFree (RsaContext);
        return EFI_INVALID_PARAMETER;
      }
      RsaFree (RsaContext);
    }

    if ((SigList->SignatureListSize - sizeof (EFI_SIGNATURE_LIST) - SigList->SignatureHeaderSize) % SigList->SignatureSize != 0) {
      return EFI_INVALID_PARAMETER;
    }
    SigCount += (SigList->SignatureListSize - sizeof (EFI_SIGNATURE_LIST) - SigList->SignatureHeaderSize) / SigList->SignatureSize;

    SigDataSize -= SigList->SignatureListSize;
    SigList = (EFI_SIGNATURE_LIST *) ((UINT8 *) SigList + SigList->SignatureListSize);
  }

  if (((UINTN) SigList - (UINTN) Data) != DataSize) {
    return EFI_INVALID_PARAMETER;
  }

  if (IsPk && SigCount > 1) {
    return EFI_INVALID_PARAMETER;
  }

  return EFI_SUCCESS;
}

/**
  Update "VendorKeys" variable to record the out of band secure boot key modification.

  @return EFI_SUCCESS           Variable is updated successfully.
  @return Others                Failed to update variable.

**/
EFI_STATUS
VendorKeyIsModified (
  VOID
  )
{
  EFI_STATUS              Status;

  if (mVendorKeyState == VENDOR_KEYS_MODIFIED) {
    return EFI_SUCCESS;
  }
  mVendorKeyState = VENDOR_KEYS_MODIFIED;

  Status = AuthServiceInternalUpdateVariable (
             EFI_VENDOR_KEYS_NV_VARIABLE_NAME,
             &gEfiVendorKeysNvGuid,
             &mVendorKeyState,
             sizeof (UINT8),
             EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_TIME_BASED_AUTHENTICATED_WRITE_ACCESS
             );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  return AuthServiceInternalUpdateVariable (
           EFI_VENDOR_KEYS_VARIABLE_NAME,
           &gEfiGlobalVariableGuid,
           &mVendorKeyState,
           sizeof (UINT8),
           EFI_VARIABLE_RUNTIME_ACCESS | EFI_VARIABLE_BOOTSERVICE_ACCESS
           );
}

/**
  Process Secure Boot Mode variable.

  Caution: This function may receive untrusted input.
  This function may be invoked in SMM mode, and datasize and data are external input.
  This function will do basic validation, before parse the data.
  This function will parse the authentication carefully to avoid security issues, like
  buffer overflow, integer overflow.
  This function will check attribute carefully to avoid authentication bypass.

  @param[in]  VariableName                Name of Variable to be found.
  @param[in]  VendorGuid                  Variable vendor GUID.
  @param[in]  Data                        Data pointer.
  @param[in]  DataSize                    Size of Data found. If size is less than the
                                          data, this value contains the required size.
  @param[in]  Attributes                  Attribute value of the variable

  @return EFI_INVALID_PARAMETER           Invalid parameter
  @return EFI_SECURITY_VIOLATION          The variable does NOT pass the validation
                                          check carried out by the firmware.
  @return EFI_WRITE_PROTECTED             Variable is Read-Only.
  @return EFI_SUCCESS                     Variable passed validation successfully.

**/
EFI_STATUS
ProcessSecureBootModeVar (
  IN  CHAR16         *VariableName,
  IN  EFI_GUID       *VendorGuid,
  IN  VOID           *Data,
  IN  UINTN          DataSize,
  IN  UINT32         Attributes OPTIONAL
  )
{
  EFI_STATUS    Status;
  VOID          *VarData;
  UINTN         VarDataSize;

  //
  // Check "AuditMode", "DeployedMode" Variable ReadWrite Attributes
  //  if in Runtime,  Always RO
  //  if in Boottime, Depends on current Secure Boot Mode
  //
  if (mAuthVarLibContextIn->AtRuntime()) {
    return EFI_WRITE_PROTECTED;
  }

  //
  // Delete not OK
  //
  if ((DataSize != sizeof(UINT8)) || (Attributes == 0)) {
    return EFI_INVALID_PARAMETER;
  }

  if (StrCmp (VariableName, EFI_AUDIT_MODE_NAME) == 0) {
    if(mSecureBootState[mSecureBootMode].IsAuditModeRO) {
      return EFI_WRITE_PROTECTED;
    }
  } else {
    //
    // Platform specific deployedMode clear. Set DeployedMode = RW
    //
    if (!InCustomMode() || !UserPhysicalPresent() || mSecureBootMode != SecureBootModeTypeDeployedMode) {
      if(mSecureBootState[mSecureBootMode].IsDeployedModeRO) {
        return EFI_WRITE_PROTECTED;
      }
    }
  }

  if (*(UINT8 *)Data != 0 && *(UINT8 *)Data != 1) {
    return EFI_INVALID_PARAMETER;
  }

  //
  // AuditMode/DeployedMode/SetupMode/SecureBoot are all NON_NV variable maintained by Variable driver
  // they can be RW. but can't be deleted. so they can always be found.
  //
  Status = AuthServiceInternalFindVariable (
             VariableName,
             VendorGuid,
             &VarData,
             &VarDataSize
             );
  if (EFI_ERROR(Status)) {
    ASSERT(FALSE);
  }

  //
  // If AuditMode/DeployedMode is assigned same value. Simply return EFI_SUCCESS
  //
  if (*(UINT8 *)VarData == *(UINT8 *)Data) {
    return EFI_SUCCESS;
  }

  //
  // Perform SecureBootMode transition
  //
  if (StrCmp (VariableName, EFI_AUDIT_MODE_NAME) == 0) {
    DEBUG((EFI_D_INFO, "Current SecureBootMode %x Transfer to SecureBootMode %x\n", mSecureBootMode, SecureBootModeTypeAuditMode));
    return SecureBootModeTransition(mSecureBootMode, SecureBootModeTypeAuditMode);
  } else if (StrCmp (VariableName, EFI_DEPLOYED_MODE_NAME) == 0) {
    if (mSecureBootMode == SecureBootModeTypeDeployedMode) {
      //
      // Platform specific DeployedMode clear. InCustomMode() && UserPhysicalPresent() is checked before
      //
      DEBUG((EFI_D_INFO, "Current SecureBootMode %x. Transfer to SecureBootMode %x\n", mSecureBootMode, SecureBootModeTypeUserMode));
      return SecureBootModeTransition(mSecureBootMode, SecureBootModeTypeUserMode);
    } else {
      DEBUG((EFI_D_INFO, "Current SecureBootMode %x. Transfer to SecureBootMode %x\n", mSecureBootMode, SecureBootModeTypeDeployedMode));
      return SecureBootModeTransition(mSecureBootMode, SecureBootModeTypeDeployedMode);
    }
  }

  return EFI_INVALID_PARAMETER;
}

/**
  Process variable with platform key for verification.

  Caution: This function may receive untrusted input.
  This function may be invoked in SMM mode, and datasize and data are external input.
  This function will do basic validation, before parse the data.
  This function will parse the authentication carefully to avoid security issues, like
  buffer overflow, integer overflow.
  This function will check attribute carefully to avoid authentication bypass.

  @param[in]  VariableName                Name of Variable to be found.
  @param[in]  VendorGuid                  Variable vendor GUID.
  @param[in]  Data                        Data pointer.
  @param[in]  DataSize                    Size of Data found. If size is less than the
                                          data, this value contains the required size.
  @param[in]  Attributes                  Attribute value of the variable
  @param[in]  IsPk                        Indicate whether it is to process pk.

  @return EFI_INVALID_PARAMETER           Invalid parameter.
  @return EFI_SECURITY_VIOLATION          The variable does NOT pass the validation.
                                          check carried out by the firmware.
  @return EFI_SUCCESS                     Variable passed validation successfully.

**/
EFI_STATUS
ProcessVarWithPk (
  IN  CHAR16                    *VariableName,
  IN  EFI_GUID                  *VendorGuid,
  IN  VOID                      *Data,
  IN  UINTN                     DataSize,
  IN  UINT32                    Attributes OPTIONAL,
  IN  BOOLEAN                   IsPk
  )
{
  EFI_STATUS                  Status;
  BOOLEAN                     Del;
  UINT8                       *Payload;
  UINTN                       PayloadSize;
  VARIABLE_ENTRY_CONSISTENCY  VariableEntry[2];

  if ((Attributes & EFI_VARIABLE_NON_VOLATILE) == 0 ||
      (Attributes & EFI_VARIABLE_TIME_BASED_AUTHENTICATED_WRITE_ACCESS) == 0) {
    //
    // PK, KEK and db/dbx/dbt should set EFI_VARIABLE_NON_VOLATILE attribute and should be a time-based
    // authenticated variable.
    //
    return EFI_INVALID_PARAMETER;
  }

  //
  // Init state of Del. State may change due to secure check
  //
  Del = FALSE;
  Payload = (UINT8 *) Data + AUTHINFO2_SIZE (Data);
  PayloadSize = DataSize - AUTHINFO2_SIZE (Data);
  if (PayloadSize == 0) {
    Del = TRUE;
  }

  //
  // Check the variable space for both PKpub and SecureBootMode variable.
  //
  VariableEntry[0].VariableSize = PayloadSize;
  VariableEntry[0].Guid         = &gEfiGlobalVariableGuid;
  VariableEntry[0].Name         = EFI_PLATFORM_KEY_NAME;

  VariableEntry[1].VariableSize = sizeof(UINT8);
  VariableEntry[1].Guid         = &gEdkiiSecureBootModeGuid;
  VariableEntry[1].Name         = EDKII_SECURE_BOOT_MODE_NAME;

  if ((InCustomMode() && UserPhysicalPresent()) || 
      (((mSecureBootMode == SecureBootModeTypeSetupMode) || (mSecureBootMode == SecureBootModeTypeAuditMode)) && !IsPk)) {

    Status = CheckSignatureListFormat(VariableName, VendorGuid, Payload, PayloadSize);
    if (EFI_ERROR (Status)) {
      return Status;
    }

    //
    // If delete PKpub, only check for "SecureBootMode" only
    // if update / add PKpub, check both NewPKpub & "SecureBootMode"
    //
    if (IsPk) {
      //
      // Delete PKpub
      //
      if (Del && ((mSecureBootMode == SecureBootModeTypeUserMode) || (mSecureBootMode == SecureBootModeTypeDeployedMode)) 
          && !mAuthVarLibContextIn->CheckRemainingSpaceForConsistency (VARIABLE_ATTRIBUTE_NV_BS_RT, &VariableEntry[1], NULL)){
        return EFI_OUT_OF_RESOURCES;
      //
      // Add PKpub
      //
      } else if (!Del && ((mSecureBootMode == SecureBootModeTypeSetupMode) || (mSecureBootMode == SecureBootModeTypeAuditMode))
                 && !mAuthVarLibContextIn->CheckRemainingSpaceForConsistency (VARIABLE_ATTRIBUTE_NV_BS_RT, &VariableEntry[0], &VariableEntry[1], NULL)) {
        return EFI_OUT_OF_RESOURCES;
      }
    }

    Status = AuthServiceInternalUpdateVariableWithTimeStamp (
               VariableName,
               VendorGuid,
               Payload,
               PayloadSize,
               Attributes,
               &((EFI_VARIABLE_AUTHENTICATION_2 *) Data)->TimeStamp
               );
    if (EFI_ERROR(Status)) {
      return Status;
    }

    if (((mSecureBootMode != SecureBootModeTypeSetupMode) && (mSecureBootMode != SecureBootModeTypeAuditMode)) || IsPk) {
      Status = VendorKeyIsModified ();
    }
  } else if (mSecureBootMode == SecureBootModeTypeUserMode || mSecureBootMode == SecureBootModeTypeDeployedMode) {
    //
    // If delete PKpub, check "SecureBootMode" only
    //
    if (IsPk && Del && !mAuthVarLibContextIn->CheckRemainingSpaceForConsistency (VARIABLE_ATTRIBUTE_NV_BS_RT, &VariableEntry[1], NULL)){
      return EFI_OUT_OF_RESOURCES;
    }

    //
    // Verify against X509 Cert in PK database.
    //
    Status = VerifyTimeBasedPayloadAndUpdate (
               VariableName,
               VendorGuid,
               Data,
               DataSize,
               Attributes,
               AuthVarTypePk,
               &Del
               );
  } else {
    //
    // SetupMode or  AuditMode to add PK
    // Verify against the certificate in data payload.
    //
    //
    // Check PKpub & SecureBootMode variable space consistency
    //
    if (!mAuthVarLibContextIn->CheckRemainingSpaceForConsistency (VARIABLE_ATTRIBUTE_NV_BS_RT, &VariableEntry[0], &VariableEntry[1], NULL)) {
      //
      // No enough variable space to set PK successfully.
      //
      return EFI_OUT_OF_RESOURCES;
    }

    Status = VerifyTimeBasedPayloadAndUpdate (
               VariableName,
               VendorGuid,
               Data,
               DataSize,
               Attributes,
               AuthVarTypePayload,
               &Del
               );
  }

  if (!EFI_ERROR(Status) && IsPk) {
    //
    // Delete or Enroll PK causes SecureBootMode change
    //
    if (!Del) {
      if (mSecureBootMode == SecureBootModeTypeSetupMode) {
        //
        // If enroll PK in setup mode,  change to user mode.
        //
        Status = SecureBootModeTransition (mSecureBootMode, SecureBootModeTypeUserMode);
      } else if (mSecureBootMode == SecureBootModeTypeAuditMode) {
        //
        // If enroll PK in Audit mode,  change to Deployed mode.
        //
        Status = SecureBootModeTransition (mSecureBootMode, SecureBootModeTypeDeployedMode);
      } else {
        DEBUG((EFI_D_INFO, "PK is updated in %x mode. No SecureBootMode change.\n", mSecureBootMode));
      }
    } else {
      if ((mSecureBootMode == SecureBootModeTypeUserMode) || (mSecureBootMode == SecureBootModeTypeDeployedMode)) {
        //
        // If delete PK in User Mode or DeployedMode,  change to Setup Mode.
        //
        Status = SecureBootModeTransition (mSecureBootMode, SecureBootModeTypeSetupMode);
      }
    }
  }

  return Status;
}

/**
  Process variable with key exchange key for verification.

  Caution: This function may receive untrusted input.
  This function may be invoked in SMM mode, and datasize and data are external input.
  This function will do basic validation, before parse the data.
  This function will parse the authentication carefully to avoid security issues, like
  buffer overflow, integer overflow.
  This function will check attribute carefully to avoid authentication bypass.

  @param[in]  VariableName                Name of Variable to be found.
  @param[in]  VendorGuid                  Variable vendor GUID.
  @param[in]  Data                        Data pointer.
  @param[in]  DataSize                    Size of Data found. If size is less than the
                                          data, this value contains the required size.
  @param[in]  Attributes                  Attribute value of the variable.

  @return EFI_INVALID_PARAMETER           Invalid parameter.
  @return EFI_SECURITY_VIOLATION          The variable does NOT pass the validation
                                          check carried out by the firmware.
  @return EFI_SUCCESS                     Variable pass validation successfully.

**/
EFI_STATUS
ProcessVarWithKek (
  IN  CHAR16                               *VariableName,
  IN  EFI_GUID                             *VendorGuid,
  IN  VOID                                 *Data,
  IN  UINTN                                DataSize,
  IN  UINT32                               Attributes OPTIONAL
  )
{
  EFI_STATUS                      Status;
  UINT8                           *Payload;
  UINTN                           PayloadSize;

  if ((Attributes & EFI_VARIABLE_NON_VOLATILE) == 0 ||
      (Attributes & EFI_VARIABLE_TIME_BASED_AUTHENTICATED_WRITE_ACCESS) == 0) {
    //
    // DB, DBX and DBT should set EFI_VARIABLE_NON_VOLATILE attribute and should be a time-based
    // authenticated variable.
    //
    return EFI_INVALID_PARAMETER;
  }

  Status = EFI_SUCCESS;
  if ((mSecureBootMode == SecureBootModeTypeUserMode || mSecureBootMode == SecureBootModeTypeDeployedMode)
   && !(InCustomMode() && UserPhysicalPresent())) {
    //
    // Time-based, verify against X509 Cert KEK.
    //
    return VerifyTimeBasedPayloadAndUpdate (
             VariableName,
             VendorGuid,
             Data,
             DataSize,
             Attributes,
             AuthVarTypeKek,
             NULL
             );
  } else {
    //
    // If in setup mode or custom secure boot mode, no authentication needed.
    //
    Payload = (UINT8 *) Data + AUTHINFO2_SIZE (Data);
    PayloadSize = DataSize - AUTHINFO2_SIZE (Data);

    Status = CheckSignatureListFormat(VariableName, VendorGuid, Payload, PayloadSize);
    if (EFI_ERROR (Status)) {
      return Status;
    }

    Status = AuthServiceInternalUpdateVariableWithTimeStamp (
               VariableName,
               VendorGuid,
               Payload,
               PayloadSize,
               Attributes,
               &((EFI_VARIABLE_AUTHENTICATION_2 *) Data)->TimeStamp
               );
    if (EFI_ERROR (Status)) {
      return Status;
    }

    if ((mSecureBootMode != SecureBootModeTypeSetupMode) && (mSecureBootMode != SecureBootModeTypeAuditMode)) {
      Status = VendorKeyIsModified ();
    }
  }

  return Status;
}

/**
  Check if it is to delete auth variable.

  @param[in] OrgAttributes      Original attribute value of the variable.
  @param[in] Data               Data pointer.
  @param[in] DataSize           Size of Data.
  @param[in] Attributes         Attribute value of the variable.

  @retval TRUE                  It is to delete auth variable.
  @retval FALSE                 It is not to delete auth variable.

**/
BOOLEAN
IsDeleteAuthVariable (
  IN  UINT32                    OrgAttributes,
  IN  VOID                      *Data,
  IN  UINTN                     DataSize,
  IN  UINT32                    Attributes
  )
{
  BOOLEAN                       Del;
  UINTN                         PayloadSize;

  Del = FALSE;

  //
  // To delete a variable created with the EFI_VARIABLE_AUTHENTICATED_WRITE_ACCESS
  // or the EFI_VARIABLE_TIME_BASED_AUTHENTICATED_WRITE_ACCESS attribute,
  // SetVariable must be used with attributes matching the existing variable
  // and the DataSize set to the size of the AuthInfo descriptor.
  //
  if ((Attributes == OrgAttributes) &&
      ((Attributes & (EFI_VARIABLE_AUTHENTICATED_WRITE_ACCESS | EFI_VARIABLE_TIME_BASED_AUTHENTICATED_WRITE_ACCESS)) != 0)) {
    if ((Attributes & EFI_VARIABLE_TIME_BASED_AUTHENTICATED_WRITE_ACCESS) != 0) {
      PayloadSize = DataSize - AUTHINFO2_SIZE (Data);
      if (PayloadSize == 0) {
        Del = TRUE;
      }
    } else {
      PayloadSize = DataSize - AUTHINFO_SIZE;
      if (PayloadSize == 0) {
        Del = TRUE;
      }
    }
  }

  return Del;
}

/**
  Process variable with EFI_VARIABLE_AUTHENTICATED_WRITE_ACCESS/EFI_VARIABLE_TIME_BASED_AUTHENTICATED_WRITE_ACCESS set

  Caution: This function may receive untrusted input.
  This function may be invoked in SMM mode, and datasize and data are external input.
  This function will do basic validation, before parse the data.
  This function will parse the authentication carefully to avoid security issues, like
  buffer overflow, integer overflow.
  This function will check attribute carefully to avoid authentication bypass.

  @param[in]  VariableName                Name of the variable.
  @param[in]  VendorGuid                  Variable vendor GUID.
  @param[in]  Data                        Data pointer.
  @param[in]  DataSize                    Size of Data.
  @param[in]  Attributes                  Attribute value of the variable.

  @return EFI_INVALID_PARAMETER           Invalid parameter.
  @return EFI_WRITE_PROTECTED             Variable is write-protected and needs authentication with
                                          EFI_VARIABLE_AUTHENTICATED_WRITE_ACCESS set.
  @return EFI_OUT_OF_RESOURCES            The Database to save the public key is full.
  @return EFI_SECURITY_VIOLATION          The variable is with EFI_VARIABLE_AUTHENTICATED_WRITE_ACCESS
                                          set, but the AuthInfo does NOT pass the validation
                                          check carried out by the firmware.
  @return EFI_SUCCESS                     Variable is not write-protected or pass validation successfully.

**/
EFI_STATUS
ProcessVariable (
  IN     CHAR16                             *VariableName,
  IN     EFI_GUID                           *VendorGuid,
  IN     VOID                               *Data,
  IN     UINTN                              DataSize,
  IN     UINT32                             Attributes
  )
{
  EFI_STATUS                      Status;
  BOOLEAN                         IsDeletion;
  BOOLEAN                         IsFirstTime;
  UINT8                           *PubKey;
  EFI_VARIABLE_AUTHENTICATION     *CertData;
  EFI_CERT_BLOCK_RSA_2048_SHA256  *CertBlock;
  UINT32                          KeyIndex;
  UINT64                          MonotonicCount;
  VARIABLE_ENTRY_CONSISTENCY      VariableDataEntry;
  UINT32                          Index;
  AUTH_VARIABLE_INFO              OrgVariableInfo;

  KeyIndex    = 0;
  CertData    = NULL;
  CertBlock   = NULL;
  PubKey      = NULL;
  IsDeletion  = FALSE;
  Status      = EFI_SUCCESS;

  ZeroMem (&OrgVariableInfo, sizeof (OrgVariableInfo));
  Status = mAuthVarLibContextIn->FindVariable (
             VariableName,
             VendorGuid,
             &OrgVariableInfo
             );

  if ((!EFI_ERROR (Status)) && IsDeleteAuthVariable (OrgVariableInfo.Attributes, Data, DataSize, Attributes) && UserPhysicalPresent()) {
    //
    // Allow the delete operation of common authenticated variable at user physical presence.
    //
    Status = AuthServiceInternalUpdateVariable (
              VariableName,
              VendorGuid,
              NULL,
              0,
              0
              );
    if (!EFI_ERROR (Status) && ((Attributes & EFI_VARIABLE_TIME_BASED_AUTHENTICATED_WRITE_ACCESS) != 0)) {
      Status = DeleteCertsFromDb (VariableName, VendorGuid, Attributes);
    }

    return Status;
  }

  if (NeedPhysicallyPresent (VariableName, VendorGuid) && !UserPhysicalPresent()) {
    //
    // This variable is protected, only physical present user could modify its value.
    //
    return EFI_SECURITY_VIOLATION;
  }

  //
  // A time-based authenticated variable and a count-based authenticated variable
  // can't be updated by each other.
  //
  if (OrgVariableInfo.Data != NULL) {
    if (((Attributes & EFI_VARIABLE_AUTHENTICATED_WRITE_ACCESS) != 0) &&
        ((OrgVariableInfo.Attributes & EFI_VARIABLE_TIME_BASED_AUTHENTICATED_WRITE_ACCESS) != 0)) {
      return EFI_SECURITY_VIOLATION;
    }

    if (((Attributes & EFI_VARIABLE_TIME_BASED_AUTHENTICATED_WRITE_ACCESS) != 0) &&
        ((OrgVariableInfo.Attributes & EFI_VARIABLE_AUTHENTICATED_WRITE_ACCESS) != 0)) {
      return EFI_SECURITY_VIOLATION;
    }
  }

  //
  // Process Time-based Authenticated variable.
  //
  if ((Attributes & EFI_VARIABLE_TIME_BASED_AUTHENTICATED_WRITE_ACCESS) != 0) {
    return VerifyTimeBasedPayloadAndUpdate (
             VariableName,
             VendorGuid,
             Data,
             DataSize,
             Attributes,
             AuthVarTypePriv,
             NULL
             );
  }

  //
  // Determine if first time SetVariable with the EFI_VARIABLE_AUTHENTICATED_WRITE_ACCESS.
  //
  if ((Attributes & EFI_VARIABLE_AUTHENTICATED_WRITE_ACCESS) != 0) {
    //
    // Determine current operation type.
    //
    if (DataSize == AUTHINFO_SIZE) {
      IsDeletion = TRUE;
    }
    //
    // Determine whether this is the first time with EFI_VARIABLE_AUTHENTICATED_WRITE_ACCESS set.
    //
    if (OrgVariableInfo.Data == NULL) {
      IsFirstTime = TRUE;
    } else if ((OrgVariableInfo.Attributes & EFI_VARIABLE_AUTHENTICATED_WRITE_ACCESS) == 0) {
      IsFirstTime = TRUE;
    } else {
      KeyIndex   = OrgVariableInfo.PubKeyIndex;
      IsFirstTime = FALSE;
    }
  } else if ((OrgVariableInfo.Data != NULL) &&
             ((OrgVariableInfo.Attributes & (EFI_VARIABLE_AUTHENTICATED_WRITE_ACCESS | EFI_VARIABLE_TIME_BASED_AUTHENTICATED_WRITE_ACCESS)) != 0)
            ) {
    //
    // If the variable is already write-protected, it always needs authentication before update.
    //
    return EFI_WRITE_PROTECTED;
  } else {
    //
    // If without EFI_VARIABLE_AUTHENTICATED_WRITE_ACCESS, set and attributes collision.
    // That means it is not authenticated variable, just update variable as usual.
    //
    Status = AuthServiceInternalUpdateVariable (VariableName, VendorGuid, Data, DataSize, Attributes);
    return Status;
  }

  //
  // Get PubKey and check Monotonic Count value corresponding to the variable.
  //
  CertData  = (EFI_VARIABLE_AUTHENTICATION *) Data;
  CertBlock = (EFI_CERT_BLOCK_RSA_2048_SHA256 *) (CertData->AuthInfo.CertData);
  PubKey    = CertBlock->PublicKey;

  //
  // Update Monotonic Count value.
  //
  MonotonicCount = CertData->MonotonicCount;

  if (!IsFirstTime) {
    //
    // 2 cases need to check here
    //   1. Internal PubKey variable. PubKeyIndex is always 0
    //   2. Other counter-based AuthVariable. Check input PubKey.
    //
    if (KeyIndex == 0) {
      return EFI_SECURITY_VIOLATION;
    }
    for (Index = 0; Index < mPubKeyNumber; Index++) {
      if (ReadUnaligned32 (&(((AUTHVAR_KEY_DB_DATA *) mPubKeyStore + Index)->KeyIndex)) == KeyIndex) {
        if (CompareMem (((AUTHVAR_KEY_DB_DATA *) mPubKeyStore + Index)->KeyData, PubKey, EFI_CERT_TYPE_RSA2048_SIZE) == 0) {
          break;
        } else {
          return EFI_SECURITY_VIOLATION;
        }
      }
    }
    if (Index == mPubKeyNumber) {
      return EFI_SECURITY_VIOLATION;
    }

    //
    // Compare the current monotonic count and ensure that it is greater than the last SetVariable
    // operation with the EFI_VARIABLE_AUTHENTICATED_WRITE_ACCESS attribute set.
    //
    if (MonotonicCount <= OrgVariableInfo.MonotonicCount) {
      //
      // Monotonic count check fail, suspicious replay attack, return EFI_SECURITY_VIOLATION.
      //
      return EFI_SECURITY_VIOLATION;
    }
  }
  //
  // Verify the certificate in Data payload.
  //
  Status = VerifyCounterBasedPayload (Data, DataSize, PubKey);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  //
  // Now, the signature has been verified!
  //
  if (IsFirstTime && !IsDeletion) {
    VariableDataEntry.VariableSize = DataSize - AUTHINFO_SIZE;
    VariableDataEntry.Guid         = VendorGuid;
    VariableDataEntry.Name         = VariableName;

    //
    // Update public key database variable if need.
    //
    KeyIndex = AddPubKeyInStore (PubKey, &VariableDataEntry);
    if (KeyIndex == 0) {
      return EFI_OUT_OF_RESOURCES;
    }
  }

  //
  // Verification pass.
  //
  return AuthServiceInternalUpdateVariableWithMonotonicCount (VariableName, VendorGuid, (UINT8*)Data + AUTHINFO_SIZE, DataSize - AUTHINFO_SIZE, Attributes, KeyIndex, MonotonicCount);
}

/**
  Filter out the duplicated EFI_SIGNATURE_DATA from the new data by comparing to the original data.

  @param[in]        Data          Pointer to original EFI_SIGNATURE_LIST.
  @param[in]        DataSize      Size of Data buffer.
  @param[in, out]   NewData       Pointer to new EFI_SIGNATURE_LIST.
  @param[in, out]   NewDataSize   Size of NewData buffer.

**/
EFI_STATUS
FilterSignatureList (
  IN     VOID       *Data,
  IN     UINTN      DataSize,
  IN OUT VOID       *NewData,
  IN OUT UINTN      *NewDataSize
  )
{
  EFI_SIGNATURE_LIST    *CertList;
  EFI_SIGNATURE_DATA    *Cert;
  UINTN                 CertCount;
  EFI_SIGNATURE_LIST    *NewCertList;
  EFI_SIGNATURE_DATA    *NewCert;
  UINTN                 NewCertCount;
  UINTN                 Index;
  UINTN                 Index2;
  UINTN                 Size;
  UINT8                 *Tail;
  UINTN                 CopiedCount;
  UINTN                 SignatureListSize;
  BOOLEAN               IsNewCert;
  UINT8                 *TempData;
  UINTN                 TempDataSize;
  EFI_STATUS            Status;

  if (*NewDataSize == 0) {
    return EFI_SUCCESS;
  }

  TempDataSize = *NewDataSize;
  Status = mAuthVarLibContextIn->GetScratchBuffer (&TempDataSize, (VOID **) &TempData);
  if (EFI_ERROR (Status)) {
    return EFI_OUT_OF_RESOURCES;
  }

  Tail = TempData;

  NewCertList = (EFI_SIGNATURE_LIST *) NewData;
  while ((*NewDataSize > 0) && (*NewDataSize >= NewCertList->SignatureListSize)) {
    NewCert      = (EFI_SIGNATURE_DATA *) ((UINT8 *) NewCertList + sizeof (EFI_SIGNATURE_LIST) + NewCertList->SignatureHeaderSize);
    NewCertCount = (NewCertList->SignatureListSize - sizeof (EFI_SIGNATURE_LIST) - NewCertList->SignatureHeaderSize) / NewCertList->SignatureSize;

    CopiedCount = 0;
    for (Index = 0; Index < NewCertCount; Index++) {
      IsNewCert = TRUE;

      Size = DataSize;
      CertList = (EFI_SIGNATURE_LIST *) Data;
      while ((Size > 0) && (Size >= CertList->SignatureListSize)) {
        if (CompareGuid (&CertList->SignatureType, &NewCertList->SignatureType) &&
           (CertList->SignatureSize == NewCertList->SignatureSize)) {
          Cert      = (EFI_SIGNATURE_DATA *) ((UINT8 *) CertList + sizeof (EFI_SIGNATURE_LIST) + CertList->SignatureHeaderSize);
          CertCount = (CertList->SignatureListSize - sizeof (EFI_SIGNATURE_LIST) - CertList->SignatureHeaderSize) / CertList->SignatureSize;
          for (Index2 = 0; Index2 < CertCount; Index2++) {
            //
            // Iterate each Signature Data in this Signature List.
            //
            if (CompareMem (NewCert, Cert, CertList->SignatureSize) == 0) {
              IsNewCert = FALSE;
              break;
            }
            Cert = (EFI_SIGNATURE_DATA *) ((UINT8 *) Cert + CertList->SignatureSize);
          }
        }

        if (!IsNewCert) {
          break;
        }
        Size -= CertList->SignatureListSize;
        CertList = (EFI_SIGNATURE_LIST *) ((UINT8 *) CertList + CertList->SignatureListSize);
      }

      if (IsNewCert) {
        //
        // New EFI_SIGNATURE_DATA, keep it.
        //
        if (CopiedCount == 0) {
          //
          // Copy EFI_SIGNATURE_LIST header for only once.
          //
          CopyMem (Tail, NewCertList, sizeof (EFI_SIGNATURE_LIST) + NewCertList->SignatureHeaderSize);
          Tail = Tail + sizeof (EFI_SIGNATURE_LIST) + NewCertList->SignatureHeaderSize;
        }

        CopyMem (Tail, NewCert, NewCertList->SignatureSize);
        Tail += NewCertList->SignatureSize;
        CopiedCount++;
      }

      NewCert = (EFI_SIGNATURE_DATA *) ((UINT8 *) NewCert + NewCertList->SignatureSize);
    }

    //
    // Update SignatureListSize in the kept EFI_SIGNATURE_LIST.
    //
    if (CopiedCount != 0) {
      SignatureListSize = sizeof (EFI_SIGNATURE_LIST) + NewCertList->SignatureHeaderSize + (CopiedCount * NewCertList->SignatureSize);
      CertList = (EFI_SIGNATURE_LIST *) (Tail - SignatureListSize);
      CertList->SignatureListSize = (UINT32) SignatureListSize;
    }

    *NewDataSize -= NewCertList->SignatureListSize;
    NewCertList = (EFI_SIGNATURE_LIST *) ((UINT8 *) NewCertList + NewCertList->SignatureListSize);
  }

  TempDataSize = (Tail - (UINT8 *) TempData);

  CopyMem (NewData, TempData, TempDataSize);
  *NewDataSize = TempDataSize;

  return EFI_SUCCESS;
}

/**
  Compare two EFI_TIME data.


  @param FirstTime           A pointer to the first EFI_TIME data.
  @param SecondTime          A pointer to the second EFI_TIME data.

  @retval  TRUE              The FirstTime is not later than the SecondTime.
  @retval  FALSE             The FirstTime is later than the SecondTime.

**/
BOOLEAN
AuthServiceInternalCompareTimeStamp (
  IN EFI_TIME               *FirstTime,
  IN EFI_TIME               *SecondTime
  )
{
  if (FirstTime->Year != SecondTime->Year) {
    return (BOOLEAN) (FirstTime->Year < SecondTime->Year);
  } else if (FirstTime->Month != SecondTime->Month) {
    return (BOOLEAN) (FirstTime->Month < SecondTime->Month);
  } else if (FirstTime->Day != SecondTime->Day) {
    return (BOOLEAN) (FirstTime->Day < SecondTime->Day);
  } else if (FirstTime->Hour != SecondTime->Hour) {
    return (BOOLEAN) (FirstTime->Hour < SecondTime->Hour);
  } else if (FirstTime->Minute != SecondTime->Minute) {
    return (BOOLEAN) (FirstTime->Minute < SecondTime->Minute);
  }

  return (BOOLEAN) (FirstTime->Second <= SecondTime->Second);
}

/**
  Find matching signer's certificates for common authenticated variable
  by corresponding VariableName and VendorGuid from "certdb" or "certdbv".

  The data format of "certdb" or "certdbv":
  //
  //     UINT32 CertDbListSize;
  // /// AUTH_CERT_DB_DATA Certs1[];
  // /// AUTH_CERT_DB_DATA Certs2[];
  // /// ...
  // /// AUTH_CERT_DB_DATA Certsn[];
  //

  @param[in]  VariableName   Name of authenticated Variable.
  @param[in]  VendorGuid     Vendor GUID of authenticated Variable.
  @param[in]  Data           Pointer to variable "certdb" or "certdbv".
  @param[in]  DataSize       Size of variable "certdb" or "certdbv".
  @param[out] CertOffset     Offset of matching CertData, from starting of Data.
  @param[out] CertDataSize   Length of CertData in bytes.
  @param[out] CertNodeOffset Offset of matching AUTH_CERT_DB_DATA , from
                             starting of Data.
  @param[out] CertNodeSize   Length of AUTH_CERT_DB_DATA in bytes.

  @retval  EFI_INVALID_PARAMETER Any input parameter is invalid.
  @retval  EFI_NOT_FOUND         Fail to find matching certs.
  @retval  EFI_SUCCESS           Find matching certs and output parameters.

**/
EFI_STATUS
FindCertsFromDb (
  IN     CHAR16           *VariableName,
  IN     EFI_GUID         *VendorGuid,
  IN     UINT8            *Data,
  IN     UINTN            DataSize,
  OUT    UINT32           *CertOffset,    OPTIONAL
  OUT    UINT32           *CertDataSize,  OPTIONAL
  OUT    UINT32           *CertNodeOffset,OPTIONAL
  OUT    UINT32           *CertNodeSize   OPTIONAL
  )
{
  UINT32                  Offset;
  AUTH_CERT_DB_DATA       *Ptr;
  UINT32                  CertSize;
  UINT32                  NameSize;
  UINT32                  NodeSize;
  UINT32                  CertDbListSize;

  if ((VariableName == NULL) || (VendorGuid == NULL) || (Data == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  //
  // Check whether DataSize matches recorded CertDbListSize.
  //
  if (DataSize < sizeof (UINT32)) {
    return EFI_INVALID_PARAMETER;
  }

  CertDbListSize = ReadUnaligned32 ((UINT32 *) Data);

  if (CertDbListSize != (UINT32) DataSize) {
    return EFI_INVALID_PARAMETER;
  }

  Offset = sizeof (UINT32);

  //
  // Get corresponding certificates by VendorGuid and VariableName.
  //
  while (Offset < (UINT32) DataSize) {
    Ptr = (AUTH_CERT_DB_DATA *) (Data + Offset);
    //
    // Check whether VendorGuid matches.
    //
    if (CompareGuid (&Ptr->VendorGuid, VendorGuid)) {
      NodeSize = ReadUnaligned32 (&Ptr->CertNodeSize);
      NameSize = ReadUnaligned32 (&Ptr->NameSize);
      CertSize = ReadUnaligned32 (&Ptr->CertDataSize);

      if (NodeSize != sizeof (EFI_GUID) + sizeof (UINT32) * 3 + CertSize +
          sizeof (CHAR16) * NameSize) {
        return EFI_INVALID_PARAMETER;
      }

      Offset = Offset + sizeof (EFI_GUID) + sizeof (UINT32) * 3;
      //
      // Check whether VariableName matches.
      //
      if ((NameSize == StrLen (VariableName)) &&
          (CompareMem (Data + Offset, VariableName, NameSize * sizeof (CHAR16)) == 0)) {
        Offset = Offset + NameSize * sizeof (CHAR16);

        if (CertOffset != NULL) {
          *CertOffset = Offset;
        }

        if (CertDataSize != NULL) {
          *CertDataSize = CertSize;
        }

        if (CertNodeOffset != NULL) {
          *CertNodeOffset = (UINT32) ((UINT8 *) Ptr - Data);
        }

        if (CertNodeSize != NULL) {
          *CertNodeSize = NodeSize;
        }

        return EFI_SUCCESS;
      } else {
        Offset = Offset + NameSize * sizeof (CHAR16) + CertSize;
      }
    } else {
      NodeSize = ReadUnaligned32 (&Ptr->CertNodeSize);
      Offset   = Offset + NodeSize;
    }
  }

  return EFI_NOT_FOUND;
}

/**
  Retrieve signer's certificates for common authenticated variable
  by corresponding VariableName and VendorGuid from "certdb"
  or "certdbv" according to authenticated variable attributes.

  @param[in]  VariableName   Name of authenticated Variable.
  @param[in]  VendorGuid     Vendor GUID of authenticated Variable.
  @param[in]  Attributes        Attributes of authenticated variable.
  @param[out] CertData       Pointer to signer's certificates.
  @param[out] CertDataSize   Length of CertData in bytes.

  @retval  EFI_INVALID_PARAMETER Any input parameter is invalid.
  @retval  EFI_NOT_FOUND         Fail to find "certdb"/"certdbv" or matching certs.
  @retval  EFI_SUCCESS           Get signer's certificates successfully.

**/
EFI_STATUS
GetCertsFromDb (
  IN     CHAR16           *VariableName,
  IN     EFI_GUID         *VendorGuid,
  IN     UINT32           Attributes,
  OUT    UINT8            **CertData,
  OUT    UINT32           *CertDataSize
  )
{
  EFI_STATUS              Status;
  UINT8                   *Data;
  UINTN                   DataSize;
  UINT32                  CertOffset;
  CHAR16                  *DbName;

  if ((VariableName == NULL) || (VendorGuid == NULL) || (CertData == NULL) || (CertDataSize == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  
  if ((Attributes & EFI_VARIABLE_NON_VOLATILE) != 0) {
    //
    // Get variable "certdb".
    //
    DbName = EFI_CERT_DB_NAME;
  } else {
    //
    // Get variable "certdbv".
    //
    DbName = EFI_CERT_DB_VOLATILE_NAME;
  }

  //
  // Get variable "certdb" or "certdbv".
  //
  Status = AuthServiceInternalFindVariable (
             DbName,
             &gEfiCertDbGuid,
             (VOID **) &Data,
             &DataSize
             );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if ((DataSize == 0) || (Data == NULL)) {
    ASSERT (FALSE);
    return EFI_NOT_FOUND;
  }

  Status = FindCertsFromDb (
             VariableName,
             VendorGuid,
             Data,
             DataSize,
             &CertOffset,
             CertDataSize,
             NULL,
             NULL
             );

  if (EFI_ERROR (Status)) {
    return Status;
  }

  *CertData = Data + CertOffset;
  return EFI_SUCCESS;
}

/**
  Delete matching signer's certificates when deleting common authenticated
  variable by corresponding VariableName and VendorGuid from "certdb" or 
  "certdbv" according to authenticated variable attributes.

  @param[in]  VariableName   Name of authenticated Variable.
  @param[in]  VendorGuid     Vendor GUID of authenticated Variable.
  @param[in]  Attributes        Attributes of authenticated variable.

  @retval  EFI_INVALID_PARAMETER Any input parameter is invalid.
  @retval  EFI_NOT_FOUND         Fail to find "certdb"/"certdbv" or matching certs.
  @retval  EFI_OUT_OF_RESOURCES  The operation is failed due to lack of resources.
  @retval  EFI_SUCCESS           The operation is completed successfully.

**/
EFI_STATUS
DeleteCertsFromDb (
  IN     CHAR16           *VariableName,
  IN     EFI_GUID         *VendorGuid,
  IN     UINT32           Attributes
  )
{
  EFI_STATUS              Status;
  UINT8                   *Data;
  UINTN                   DataSize;
  UINT32                  VarAttr;
  UINT32                  CertNodeOffset;
  UINT32                  CertNodeSize;
  UINT8                   *NewCertDb;
  UINT32                  NewCertDbSize;
  CHAR16                  *DbName;

  if ((VariableName == NULL) || (VendorGuid == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  if ((Attributes & EFI_VARIABLE_NON_VOLATILE) != 0) {
    //
    // Get variable "certdb".
    //
    DbName = EFI_CERT_DB_NAME;
    VarAttr  = EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_RUNTIME_ACCESS | EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_TIME_BASED_AUTHENTICATED_WRITE_ACCESS;
  } else {
    //
    // Get variable "certdbv".
    //
    DbName = EFI_CERT_DB_VOLATILE_NAME;
    VarAttr = EFI_VARIABLE_RUNTIME_ACCESS | EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_TIME_BASED_AUTHENTICATED_WRITE_ACCESS;
  }

  Status = AuthServiceInternalFindVariable (
             DbName,
             &gEfiCertDbGuid,
             (VOID **) &Data,
             &DataSize
             );

  if (EFI_ERROR (Status)) {
    return Status;
  }

  if ((DataSize == 0) || (Data == NULL)) {
    ASSERT (FALSE);
    return EFI_NOT_FOUND;
  }

  if (DataSize == sizeof (UINT32)) {
    //
    // There is no certs in "certdb" or "certdbv".
    //
    return EFI_SUCCESS;
  }

  //
  // Get corresponding cert node from "certdb" or "certdbv".
  //
  Status = FindCertsFromDb (
             VariableName,
             VendorGuid,
             Data,
             DataSize,
             NULL,
             NULL,
             &CertNodeOffset,
             &CertNodeSize
             );

  if (EFI_ERROR (Status)) {
    return Status;
  }

  if (DataSize < (CertNodeOffset + CertNodeSize)) {
    return EFI_NOT_FOUND;
  }

  //
  // Construct new data content of variable "certdb" or "certdbv".
  //
  NewCertDbSize = (UINT32) DataSize - CertNodeSize;
  NewCertDb     = (UINT8*) mCertDbStore;

  //
  // Copy the DB entries before deleting node.
  //
  CopyMem (NewCertDb, Data, CertNodeOffset);
  //
  // Update CertDbListSize.
  //
  CopyMem (NewCertDb, &NewCertDbSize, sizeof (UINT32));
  //
  // Copy the DB entries after deleting node.
  //
  if (DataSize > (CertNodeOffset + CertNodeSize)) {
    CopyMem (
      NewCertDb + CertNodeOffset,
      Data + CertNodeOffset + CertNodeSize,
      DataSize - CertNodeOffset - CertNodeSize
      );
  }

  //
  // Set "certdb" or "certdbv".
  //
  Status   = AuthServiceInternalUpdateVariable (
               DbName,
               &gEfiCertDbGuid,
               NewCertDb,
               NewCertDbSize,
               VarAttr
               );

  return Status;
}

/**
  Insert signer's certificates for common authenticated variable with VariableName
  and VendorGuid in AUTH_CERT_DB_DATA to "certdb" or "certdbv" according to
  time based authenticated variable attributes.

  @param[in]  VariableName   Name of authenticated Variable.
  @param[in]  VendorGuid     Vendor GUID of authenticated Variable.
  @param[in]  Attributes     Attributes of authenticated variable.
  @param[in]  CertData       Pointer to signer's certificates.
  @param[in]  CertDataSize   Length of CertData in bytes.

  @retval  EFI_INVALID_PARAMETER Any input parameter is invalid.
  @retval  EFI_ACCESS_DENIED     An AUTH_CERT_DB_DATA entry with same VariableName
                                 and VendorGuid already exists.
  @retval  EFI_OUT_OF_RESOURCES  The operation is failed due to lack of resources.
  @retval  EFI_SUCCESS           Insert an AUTH_CERT_DB_DATA entry to "certdb" or "certdbv"

**/
EFI_STATUS
InsertCertsToDb (
  IN     CHAR16           *VariableName,
  IN     EFI_GUID         *VendorGuid,
  IN     UINT32           Attributes,
  IN     UINT8            *CertData,
  IN     UINTN            CertDataSize
  )
{
  EFI_STATUS              Status;
  UINT8                   *Data;
  UINTN                   DataSize;
  UINT32                  VarAttr;
  UINT8                   *NewCertDb;
  UINT32                  NewCertDbSize;
  UINT32                  CertNodeSize;
  UINT32                  NameSize;
  AUTH_CERT_DB_DATA       *Ptr;
  CHAR16                  *DbName;

  if ((VariableName == NULL) || (VendorGuid == NULL) || (CertData == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  if ((Attributes & EFI_VARIABLE_NON_VOLATILE) != 0) {
    //
    // Get variable "certdb".
    //
    DbName = EFI_CERT_DB_NAME;
    VarAttr  = EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_RUNTIME_ACCESS | EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_TIME_BASED_AUTHENTICATED_WRITE_ACCESS;
  } else {
    //
    // Get variable "certdbv".
    //
    DbName = EFI_CERT_DB_VOLATILE_NAME;
    VarAttr = EFI_VARIABLE_RUNTIME_ACCESS | EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_TIME_BASED_AUTHENTICATED_WRITE_ACCESS;
  }

  //
  // Get variable "certdb" or "certdbv".
  //
  Status = AuthServiceInternalFindVariable (
             DbName,
             &gEfiCertDbGuid,
             (VOID **) &Data,
             &DataSize
             );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if ((DataSize == 0) || (Data == NULL)) {
    ASSERT (FALSE);
    return EFI_NOT_FOUND;
  }

  //
  // Find whether matching cert node already exists in "certdb" or "certdbv".
  // If yes return error.
  //
  Status = FindCertsFromDb (
             VariableName,
             VendorGuid,
             Data,
             DataSize,
             NULL,
             NULL,
             NULL,
             NULL
             );

  if (!EFI_ERROR (Status)) {
    ASSERT (FALSE);
    return EFI_ACCESS_DENIED;
  }

  //
  // Construct new data content of variable "certdb" or "certdbv".
  //
  NameSize      = (UINT32) StrLen (VariableName);
  CertNodeSize  = sizeof (AUTH_CERT_DB_DATA) + (UINT32) CertDataSize + NameSize * sizeof (CHAR16);
  NewCertDbSize = (UINT32) DataSize + CertNodeSize;
  if (NewCertDbSize > mMaxCertDbSize) {
    return EFI_OUT_OF_RESOURCES;
  }
  NewCertDb     = (UINT8*) mCertDbStore;

  //
  // Copy the DB entries before inserting node.
  //
  CopyMem (NewCertDb, Data, DataSize);
  //
  // Update CertDbListSize.
  //
  CopyMem (NewCertDb, &NewCertDbSize, sizeof (UINT32));
  //
  // Construct new cert node.
  //
  Ptr = (AUTH_CERT_DB_DATA *) (NewCertDb + DataSize);
  CopyGuid (&Ptr->VendorGuid, VendorGuid);
  CopyMem (&Ptr->CertNodeSize, &CertNodeSize, sizeof (UINT32));
  CopyMem (&Ptr->NameSize, &NameSize, sizeof (UINT32));
  CopyMem (&Ptr->CertDataSize, &CertDataSize, sizeof (UINT32));

  CopyMem (
    (UINT8 *) Ptr + sizeof (AUTH_CERT_DB_DATA),
    VariableName,
    NameSize * sizeof (CHAR16)
    );

  CopyMem (
    (UINT8 *) Ptr +  sizeof (AUTH_CERT_DB_DATA) + NameSize * sizeof (CHAR16),
    CertData,
    CertDataSize
    );

  //
  // Set "certdb" or "certdbv".
  //
  Status   = AuthServiceInternalUpdateVariable (
               DbName,
               &gEfiCertDbGuid,
               NewCertDb,
               NewCertDbSize,
               VarAttr
               );

  return Status;
}

/**
  Clean up signer's certificates for common authenticated variable
  by corresponding VariableName and VendorGuid from "certdb".
  System may break down during Timebased Variable update & certdb update,
  make them inconsistent,  this function is called in AuthVariable Init
  to ensure consistency.

  @retval  EFI_NOT_FOUND         Fail to find variable "certdb".
  @retval  EFI_OUT_OF_RESOURCES  The operation is failed due to lack of resources.
  @retval  EFI_SUCCESS           The operation is completed successfully.

**/
EFI_STATUS
CleanCertsFromDb (
  VOID
  )
{
  UINT32                  Offset;
  AUTH_CERT_DB_DATA       *Ptr;
  UINT32                  NameSize;
  UINT32                  NodeSize;
  CHAR16                  *VariableName;
  EFI_STATUS              Status;
  BOOLEAN                 CertCleaned;
  UINT8                   *Data;
  UINTN                   DataSize;
  EFI_GUID                AuthVarGuid;
  AUTH_VARIABLE_INFO      AuthVariableInfo;

  Status = EFI_SUCCESS;

  //
  // Get corresponding certificates by VendorGuid and VariableName.
  //
  do {
    CertCleaned = FALSE;

    //
    // Get latest variable "certdb"
    //
    Status = AuthServiceInternalFindVariable (
               EFI_CERT_DB_NAME,
               &gEfiCertDbGuid,
               (VOID **) &Data,
               &DataSize
               );
    if (EFI_ERROR (Status)) {
      return Status;
    }

    if ((DataSize == 0) || (Data == NULL)) {
      ASSERT (FALSE);
      return EFI_NOT_FOUND;
    }

    Offset = sizeof (UINT32);

    while (Offset < (UINT32) DataSize) {
      Ptr = (AUTH_CERT_DB_DATA *) (Data + Offset);
      NodeSize = ReadUnaligned32 (&Ptr->CertNodeSize);
      NameSize = ReadUnaligned32 (&Ptr->NameSize);

      //
      // Get VarName tailed with '\0'
      //
      VariableName = AllocateZeroPool((NameSize + 1) * sizeof(CHAR16));
      if (VariableName == NULL) {
        return EFI_OUT_OF_RESOURCES;
      }
      CopyMem (VariableName, (UINT8 *) Ptr + sizeof (AUTH_CERT_DB_DATA), NameSize * sizeof(CHAR16));
      //
      // Keep VarGuid  aligned
      //
      CopyMem (&AuthVarGuid, &Ptr->VendorGuid, sizeof(EFI_GUID));

      //
      // Find corresponding time auth variable
      //
      ZeroMem (&AuthVariableInfo, sizeof (AuthVariableInfo));
      Status = mAuthVarLibContextIn->FindVariable (
                                       VariableName,
                                       &AuthVarGuid,
                                       &AuthVariableInfo
                                       );

      if (EFI_ERROR(Status)) {
        Status      = DeleteCertsFromDb(
                        VariableName,
                        &AuthVarGuid,
                        AuthVariableInfo.Attributes
                        );
        CertCleaned = TRUE;
        DEBUG((EFI_D_INFO, "Recovery!! Cert for Auth Variable %s Guid %g is removed for consistency\n", VariableName, &AuthVarGuid));
        FreePool(VariableName);
        break;
      }

      FreePool(VariableName);
      Offset = Offset + NodeSize;
    }
  } while (CertCleaned);

  return Status;
}

/**
  Process variable with EFI_VARIABLE_TIME_BASED_AUTHENTICATED_WRITE_ACCESS set

  Caution: This function may receive untrusted input.
  This function may be invoked in SMM mode, and datasize and data are external input.
  This function will do basic validation, before parse the data.
  This function will parse the authentication carefully to avoid security issues, like
  buffer overflow, integer overflow.

  @param[in]  VariableName                Name of Variable to be found.
  @param[in]  VendorGuid                  Variable vendor GUID.
  @param[in]  Data                        Data pointer.
  @param[in]  DataSize                    Size of Data found. If size is less than the
                                          data, this value contains the required size.
  @param[in]  Attributes                  Attribute value of the variable.
  @param[in]  AuthVarType                 Verify against PK, KEK database, private database or certificate in data payload.
  @param[in]  OrgTimeStamp                Pointer to original time stamp,
                                          original variable is not found if NULL.
  @param[out]  VarPayloadPtr              Pointer to variable payload address.
  @param[out]  VarPayloadSize             Pointer to variable payload size.

  @retval EFI_INVALID_PARAMETER           Invalid parameter.
  @retval EFI_SECURITY_VIOLATION          The variable does NOT pass the validation
                                          check carried out by the firmware.
  @retval EFI_OUT_OF_RESOURCES            Failed to process variable due to lack
                                          of resources.
  @retval EFI_SUCCESS                     Variable pass validation successfully.

**/
EFI_STATUS
VerifyTimeBasedPayload (
  IN     CHAR16                             *VariableName,
  IN     EFI_GUID                           *VendorGuid,
  IN     VOID                               *Data,
  IN     UINTN                              DataSize,
  IN     UINT32                             Attributes,
  IN     AUTHVAR_TYPE                       AuthVarType,
  IN     EFI_TIME                           *OrgTimeStamp,
  OUT    UINT8                              **VarPayloadPtr,
  OUT    UINTN                              *VarPayloadSize
  )
{
  EFI_VARIABLE_AUTHENTICATION_2    *CertData;
  UINT8                            *SigData;
  UINT32                           SigDataSize;
  UINT8                            *PayloadPtr;
  UINTN                            PayloadSize;
  UINT32                           Attr;
  BOOLEAN                          VerifyStatus;
  EFI_STATUS                       Status;
  EFI_SIGNATURE_LIST               *CertList;
  EFI_SIGNATURE_DATA               *Cert;
  UINTN                            Index;
  UINTN                            CertCount;
  UINT32                           KekDataSize;
  UINT8                            *NewData;
  UINTN                            NewDataSize;
  UINT8                            *Buffer;
  UINTN                            Length;
  UINT8                            *RootCert;
  UINTN                            RootCertSize;
  UINT8                            *SignerCerts;
  UINTN                            CertStackSize;
  UINT8                            *CertsInCertDb;
  UINT32                           CertsSizeinDb;

  VerifyStatus           = FALSE;
  CertData               = NULL;
  NewData                = NULL;
  Attr                   = Attributes;
  SignerCerts            = NULL;
  RootCert               = NULL;
  CertsInCertDb          = NULL;

  //
  // When the attribute EFI_VARIABLE_TIME_BASED_AUTHENTICATED_WRITE_ACCESS is
  // set, then the Data buffer shall begin with an instance of a complete (and serialized)
  // EFI_VARIABLE_AUTHENTICATION_2 descriptor. The descriptor shall be followed by the new
  // variable value and DataSize shall reflect the combined size of the descriptor and the new
  // variable value. The authentication descriptor is not part of the variable data and is not
  // returned by subsequent calls to GetVariable().
  //
  CertData = (EFI_VARIABLE_AUTHENTICATION_2 *) Data;

  //
  // Verify that Pad1, Nanosecond, TimeZone, Daylight and Pad2 components of the
  // TimeStamp value are set to zero.
  //
  if ((CertData->TimeStamp.Pad1 != 0) ||
      (CertData->TimeStamp.Nanosecond != 0) ||
      (CertData->TimeStamp.TimeZone != 0) ||
      (CertData->TimeStamp.Daylight != 0) ||
      (CertData->TimeStamp.Pad2 != 0)) {
    return EFI_SECURITY_VIOLATION;
  }

  if ((OrgTimeStamp != NULL) && ((Attributes & EFI_VARIABLE_APPEND_WRITE) == 0)) {
    if (AuthServiceInternalCompareTimeStamp (&CertData->TimeStamp, OrgTimeStamp)) {
      //
      // TimeStamp check fail, suspicious replay attack, return EFI_SECURITY_VIOLATION.
      //
      return EFI_SECURITY_VIOLATION;
    }
  }

  //
  // wCertificateType should be WIN_CERT_TYPE_EFI_GUID.
  // Cert type should be EFI_CERT_TYPE_PKCS7_GUID.
  //
  if ((CertData->AuthInfo.Hdr.wCertificateType != WIN_CERT_TYPE_EFI_GUID) ||
      !CompareGuid (&CertData->AuthInfo.CertType, &gEfiCertPkcs7Guid)) {
    //
    // Invalid AuthInfo type, return EFI_SECURITY_VIOLATION.
    //
    return EFI_SECURITY_VIOLATION;
  }

  //
  // Find out Pkcs7 SignedData which follows the EFI_VARIABLE_AUTHENTICATION_2 descriptor.
  // AuthInfo.Hdr.dwLength is the length of the entire certificate, including the length of the header.
  //
  SigData = CertData->AuthInfo.CertData;
  SigDataSize = CertData->AuthInfo.Hdr.dwLength - (UINT32) (OFFSET_OF (WIN_CERTIFICATE_UEFI_GUID, CertData));

  //
  // Find out the new data payload which follows Pkcs7 SignedData directly.
  //
  PayloadPtr = SigData + SigDataSize;
  PayloadSize = DataSize - OFFSET_OF_AUTHINFO2_CERT_DATA - (UINTN) SigDataSize;

  //
  // Construct a serialization buffer of the values of the VariableName, VendorGuid and Attributes
  // parameters of the SetVariable() call and the TimeStamp component of the
  // EFI_VARIABLE_AUTHENTICATION_2 descriptor followed by the variable's new value
  // i.e. (VariableName, VendorGuid, Attributes, TimeStamp, Data)
  //
  NewDataSize = PayloadSize + sizeof (EFI_TIME) + sizeof (UINT32) +
                sizeof (EFI_GUID) + StrSize (VariableName) - sizeof (CHAR16);

  //
  // Here is to reuse scratch data area(at the end of volatile variable store)
  // to reduce SMRAM consumption for SMM variable driver.
  // The scratch buffer is enough to hold the serialized data and safe to use,
  // because it is only used at here to do verification temporarily first
  // and then used in UpdateVariable() for a time based auth variable set.
  //
  Status = mAuthVarLibContextIn->GetScratchBuffer (&NewDataSize, (VOID **) &NewData);
  if (EFI_ERROR (Status)) {
    return EFI_OUT_OF_RESOURCES;
  }

  Buffer = NewData;
  Length = StrLen (VariableName) * sizeof (CHAR16);
  CopyMem (Buffer, VariableName, Length);
  Buffer += Length;

  Length = sizeof (EFI_GUID);
  CopyMem (Buffer, VendorGuid, Length);
  Buffer += Length;

  Length = sizeof (UINT32);
  CopyMem (Buffer, &Attr, Length);
  Buffer += Length;

  Length = sizeof (EFI_TIME);
  CopyMem (Buffer, &CertData->TimeStamp, Length);
  Buffer += Length;

  CopyMem (Buffer, PayloadPtr, PayloadSize);

  if (AuthVarType == AuthVarTypePk) {
    //
    // Verify that the signature has been made with the current Platform Key (no chaining for PK).
    // First, get signer's certificates from SignedData.
    //
    VerifyStatus = Pkcs7GetSigners (
                     SigData,
                     SigDataSize,
                     &SignerCerts,
                     &CertStackSize,
                     &RootCert,
                     &RootCertSize
                     );
    if (!VerifyStatus) {
      goto Exit;
    }

    //
    // Second, get the current platform key from variable. Check whether it's identical with signer's certificates
    // in SignedData. If not, return error immediately.
    //
    Status = AuthServiceInternalFindVariable (
               EFI_PLATFORM_KEY_NAME,
               &gEfiGlobalVariableGuid,
               &Data,
               &DataSize
               );
    if (EFI_ERROR (Status)) {
      VerifyStatus = FALSE;
      goto Exit;
    }
    CertList = (EFI_SIGNATURE_LIST *) Data;
    Cert     = (EFI_SIGNATURE_DATA *) ((UINT8 *) CertList + sizeof (EFI_SIGNATURE_LIST) + CertList->SignatureHeaderSize);
    if ((RootCertSize != (CertList->SignatureSize - (sizeof (EFI_SIGNATURE_DATA) - 1))) ||
        (CompareMem (Cert->SignatureData, RootCert, RootCertSize) != 0)) {
      VerifyStatus = FALSE;
      goto Exit;
    }

    //
    // Verify Pkcs7 SignedData via Pkcs7Verify library.
    //
    VerifyStatus = Pkcs7Verify (
                     SigData,
                     SigDataSize,
                     RootCert,
                     RootCertSize,
                     NewData,
                     NewDataSize
                     );

  } else if (AuthVarType == AuthVarTypeKek) {

    //
    // Get KEK database from variable.
    //
    Status = AuthServiceInternalFindVariable (
               EFI_KEY_EXCHANGE_KEY_NAME,
               &gEfiGlobalVariableGuid,
               &Data,
               &DataSize
               );
    if (EFI_ERROR (Status)) {
      return Status;
    }

    //
    // Ready to verify Pkcs7 SignedData. Go through KEK Signature Database to find out X.509 CertList.
    //
    KekDataSize      = (UINT32) DataSize;
    CertList         = (EFI_SIGNATURE_LIST *) Data;
    while ((KekDataSize > 0) && (KekDataSize >= CertList->SignatureListSize)) {
      if (CompareGuid (&CertList->SignatureType, &gEfiCertX509Guid)) {
        Cert       = (EFI_SIGNATURE_DATA *) ((UINT8 *) CertList + sizeof (EFI_SIGNATURE_LIST) + CertList->SignatureHeaderSize);
        CertCount  = (CertList->SignatureListSize - sizeof (EFI_SIGNATURE_LIST) - CertList->SignatureHeaderSize) / CertList->SignatureSize;
        for (Index = 0; Index < CertCount; Index++) {
          //
          // Iterate each Signature Data Node within this CertList for a verify
          //
          RootCert      = Cert->SignatureData;
          RootCertSize  = CertList->SignatureSize - (sizeof (EFI_SIGNATURE_DATA) - 1);

          //
          // Verify Pkcs7 SignedData via Pkcs7Verify library.
          //
          VerifyStatus = Pkcs7Verify (
                           SigData,
                           SigDataSize,
                           RootCert,
                           RootCertSize,
                           NewData,
                           NewDataSize
                           );
          if (VerifyStatus) {
            goto Exit;
          }
          Cert = (EFI_SIGNATURE_DATA *) ((UINT8 *) Cert + CertList->SignatureSize);
        }
      }
      KekDataSize -= CertList->SignatureListSize;
      CertList = (EFI_SIGNATURE_LIST *) ((UINT8 *) CertList + CertList->SignatureListSize);
    }
  } else if (AuthVarType == AuthVarTypePriv) {

    //
    // Process common authenticated variable except PK/KEK/DB/DBX/DBT.
    // Get signer's certificates from SignedData.
    //
    VerifyStatus = Pkcs7GetSigners (
                     SigData,
                     SigDataSize,
                     &SignerCerts,
                     &CertStackSize,
                     &RootCert,
                     &RootCertSize
                     );
    if (!VerifyStatus) {
      goto Exit;
    }

    //
    // Get previously stored signer's certificates from certdb or certdbv for existing
    // variable. Check whether they are identical with signer's certificates
    // in SignedData. If not, return error immediately.
    //
    if (OrgTimeStamp != NULL) {
      VerifyStatus = FALSE;

      Status = GetCertsFromDb (VariableName, VendorGuid, Attributes, &CertsInCertDb, &CertsSizeinDb);
      if (EFI_ERROR (Status)) {
        goto Exit;
      }

      if ((CertStackSize != CertsSizeinDb) ||
          (CompareMem (SignerCerts, CertsInCertDb, CertsSizeinDb) != 0)) {
        goto Exit;
      }
    }

    VerifyStatus = Pkcs7Verify (
                     SigData,
                     SigDataSize,
                     RootCert,
                     RootCertSize,
                     NewData,
                     NewDataSize
                     );
    if (!VerifyStatus) {
      goto Exit;
    }

    if ((OrgTimeStamp == NULL) && (PayloadSize != 0)) {
      //
      // Insert signer's certificates when adding a new common authenticated variable.
      //
      Status = InsertCertsToDb (VariableName, VendorGuid, Attributes, SignerCerts, CertStackSize);
      if (EFI_ERROR (Status)) {
        VerifyStatus = FALSE;
        goto Exit;
      }
    }
  } else if (AuthVarType == AuthVarTypePayload) {
    CertList = (EFI_SIGNATURE_LIST *) PayloadPtr;
    Cert     = (EFI_SIGNATURE_DATA *) ((UINT8 *) CertList + sizeof (EFI_SIGNATURE_LIST) + CertList->SignatureHeaderSize);
    RootCert      = Cert->SignatureData;
    RootCertSize  = CertList->SignatureSize - (sizeof (EFI_SIGNATURE_DATA) - 1);
    //
    // Verify Pkcs7 SignedData via Pkcs7Verify library.
    //
    VerifyStatus = Pkcs7Verify (
                     SigData,
                     SigDataSize,
                     RootCert,
                     RootCertSize,
                     NewData,
                     NewDataSize
                     );
  } else {
    return EFI_SECURITY_VIOLATION;
  }

Exit:

  if (AuthVarType == AuthVarTypePk || AuthVarType == AuthVarTypePriv) {
    Pkcs7FreeSigners (RootCert);
    Pkcs7FreeSigners (SignerCerts);
  }

  if (!VerifyStatus) {
    return EFI_SECURITY_VIOLATION;
  }

  Status = CheckSignatureListFormat(VariableName, VendorGuid, PayloadPtr, PayloadSize);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  *VarPayloadPtr = PayloadPtr;
  *VarPayloadSize = PayloadSize;

  return EFI_SUCCESS;
}

/**
  Process variable with EFI_VARIABLE_TIME_BASED_AUTHENTICATED_WRITE_ACCESS set

  Caution: This function may receive untrusted input.
  This function may be invoked in SMM mode, and datasize and data are external input.
  This function will do basic validation, before parse the data.
  This function will parse the authentication carefully to avoid security issues, like
  buffer overflow, integer overflow.

  @param[in]  VariableName                Name of Variable to be found.
  @param[in]  VendorGuid                  Variable vendor GUID.
  @param[in]  Data                        Data pointer.
  @param[in]  DataSize                    Size of Data found. If size is less than the
                                          data, this value contains the required size.
  @param[in]  Attributes                  Attribute value of the variable.
  @param[in]  AuthVarType                 Verify against PK, KEK database, private database or certificate in data payload.
  @param[out] VarDel                      Delete the variable or not.

  @retval EFI_INVALID_PARAMETER           Invalid parameter.
  @retval EFI_SECURITY_VIOLATION          The variable does NOT pass the validation
                                          check carried out by the firmware.
  @retval EFI_OUT_OF_RESOURCES            Failed to process variable due to lack
                                          of resources.
  @retval EFI_SUCCESS                     Variable pass validation successfully.

**/
EFI_STATUS
VerifyTimeBasedPayloadAndUpdate (
  IN     CHAR16                             *VariableName,
  IN     EFI_GUID                           *VendorGuid,
  IN     VOID                               *Data,
  IN     UINTN                              DataSize,
  IN     UINT32                             Attributes,
  IN     AUTHVAR_TYPE                       AuthVarType,
  OUT    BOOLEAN                            *VarDel
  )
{
  EFI_STATUS                       Status;
  EFI_STATUS                       FindStatus;
  UINT8                            *PayloadPtr;
  UINTN                            PayloadSize;
  EFI_VARIABLE_AUTHENTICATION_2    *CertData;
  AUTH_VARIABLE_INFO               OrgVariableInfo;
  BOOLEAN                          IsDel;

  ZeroMem (&OrgVariableInfo, sizeof (OrgVariableInfo));
  FindStatus = mAuthVarLibContextIn->FindVariable (
             VariableName,
             VendorGuid,
             &OrgVariableInfo
             );

  Status = VerifyTimeBasedPayload (
             VariableName,
             VendorGuid,
             Data,
             DataSize,
             Attributes,
             AuthVarType,
             (!EFI_ERROR (FindStatus)) ? OrgVariableInfo.TimeStamp : NULL,
             &PayloadPtr,
             &PayloadSize
             );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if (!EFI_ERROR(FindStatus)
   && (PayloadSize == 0)
   && ((Attributes & EFI_VARIABLE_APPEND_WRITE) == 0)) {
    IsDel = TRUE;
  } else {
    IsDel = FALSE;
  }

  CertData = (EFI_VARIABLE_AUTHENTICATION_2 *) Data;

  //
  // Final step: Update/Append Variable if it pass Pkcs7Verify
  //
  Status = AuthServiceInternalUpdateVariableWithTimeStamp (
             VariableName,
             VendorGuid,
             PayloadPtr,
             PayloadSize,
             Attributes,
             &CertData->TimeStamp
             );

  //
  // Delete signer's certificates when delete the common authenticated variable.
  //
  if (IsDel && AuthVarType == AuthVarTypePriv && !EFI_ERROR(Status) ) {
    Status = DeleteCertsFromDb (VariableName, VendorGuid, Attributes);
  }

  if (VarDel != NULL) {
    if (IsDel && !EFI_ERROR(Status)) {
      *VarDel = TRUE;
    } else {
      *VarDel = FALSE;
    }
  }

  return Status;
}
