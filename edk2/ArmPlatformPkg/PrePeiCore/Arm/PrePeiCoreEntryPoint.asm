//
//  Copyright (c) 2011-2013, ARM Limited. All rights reserved.
//
//  This program and the accompanying materials
//  are licensed and made available under the terms and conditions of the BSD License
//  which accompanies this distribution.  The full text of the license may be found at
//  http://opensource.org/licenses/bsd-license.php
//
//  THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
//  WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.
//
//

#include <AsmMacroIoLib.h>
#include <Base.h>
#include <Library/PcdLib.h>
#include <AutoGen.h>

  INCLUDE AsmMacroIoLib.inc

  IMPORT  CEntryPoint
  IMPORT  ArmPlatformGetCorePosition
  IMPORT  ArmPlatformIsPrimaryCore
  IMPORT  ArmReadMpidr
  IMPORT  ArmPlatformPeiBootAction
  EXPORT  _ModuleEntryPoint

  PRESERVE8
  AREA    PrePeiCoreEntryPoint, CODE, READONLY

StartupAddr        DCD      CEntryPoint

_ModuleEntryPoint
  // Do early platform specific actions
  bl    ArmPlatformPeiBootAction

  // Identify CPU ID
  bl    ArmReadMpidr
  // Keep a copy of the MpId register value
  mov   r5, r0

  // Is it the Primary Core ?
  bl    ArmPlatformIsPrimaryCore

  // Get the top of the primary stacks (and the base of the secondary stacks)
  LoadConstantToReg (FixedPcdGet32(PcdCPUCoresStackBase), r1)
  LoadConstantToReg (FixedPcdGet32(PcdCPUCorePrimaryStackSize), r2)
  add   r1, r1, r2

  // r0 is equal to 1 if I am the primary core
  cmp   r0, #1
  beq   _SetupPrimaryCoreStack

_SetupSecondaryCoreStack
  // r1 contains the base of the secondary stacks

  // Get the Core Position
  mov   r6, r1      // Save base of the secondary stacks
  mov   r0, r5
  bl    ArmPlatformGetCorePosition
  // The stack starts at the top of the stack region. Add '1' to the Core Position to get the top of the stack
  add   r0, r0, #1

  // StackOffset = CorePos * StackSize
  LoadConstantToReg (FixedPcdGet32(PcdCPUCoreSecondaryStackSize), r2)
  mul   r0, r0, r2
  // SP = StackBase + StackOffset
  add   sp, r6, r0

_PrepareArguments
  // The PEI Core Entry Point has been computed by GenFV and stored in the second entry of the Reset Vector
  LoadConstantToReg (FixedPcdGet32(PcdFvBaseAddress), r2)
  add   r2, r2, #4
  ldr   r1, [r2]

  // Move sec startup address into a data register
  // Ensure we're jumping to FV version of the code (not boot remapped alias)
  ldr   r3, StartupAddr

  // Jump to PrePeiCore C code
  //    r0 = mp_id
  //    r1 = pei_core_address
  mov   r0, r5
  blx   r3

_SetupPrimaryCoreStack
  mov   sp, r1
  b     _PrepareArguments

_NeverReturn
  b _NeverReturn

  END
