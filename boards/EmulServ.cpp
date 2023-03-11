/*++

 Copyright (c) 2006 Microsoft Corporation.  All rights reserved.

 The use and distribution terms for this software are contained in the file
 named license.rtf, which can be found in the root of this distribution.
 By using this software in any fashion, you are agreeing to be bound by the
 terms of this license.

 You must not remove this notice, or any other, from this software.



EmulServ is a general-purpose emulator-to-guest communication mechanism.  Software
inside the emulator can call EmulServ.RaiseInterrupt(EmulServ_...) to alert the
EmulServ device that some action should be taken by the guest if it cares to.

The primary use is to notify the guest when Folder Sharing is enabled or disabled,
but it could also be used to notify the guest when screen rotation should take place,
or any other interesting event.

Within the guest, the emulserv device driver monitors a single interrupt (EINT11
on SMDK2410) and contains a hard-coded dispatcher which decodes the contents
of the IOEmulServ InterruptPending register and forwards the notification to
whatever code is interested in it.

Guest OS Usage:
- notifications are disabled by default
- to enable notifications:
  - install a SYSINTR_EMULSERV interrupt handler
  - write 0xffffffff to InterruptMask (offset 0) to unmask all emulserv subinterrupts
- when the interrupt is raised:
  - read the 4-byte InterrupPending register (offset 4) and dispatch based on it.  Reading
    from the register clears all pending interrupts and lowers SYSINTR_EMULSERV
- to disable notifications:
  - write 0 to the InterruptMask
  - uninstall the SYSINTR_EMULSERV interrupt handler

--*/

#include "emulator.h"
#include "Config.h"
#include "MappedIO.h"
#include "Board.h"
#include "resource.h"
#include "devices.h"
#include "EmulServ.h"

// The EmulServ device uses EINT11.  If this code is to be used for different board emulators,
// then the constant should be changed into an accessor function implemented in board.h/board.cpp.
#define EmulServInterruptNumber 11  

void __fastcall IOEmulServ::SaveState(StateFiler& filer) const
{
    filer.Write('IEMS');
    filer.Write(InterruptMask);
}

void __fastcall IOEmulServ::RestoreState(StateFiler& filer)
{
    filer.Verify('IEMS');
    filer.Read(InterruptMask);
}

unsigned __int32 IOEmulServ::ReadWord(unsigned __int32 IOAddress)
{
    switch (IOAddress) {
    case 0:
        return InterruptMask;

    case 4:
        {
            unsigned __int32 RetVal;

            RetVal = InterruptPending;
            InterruptPending = 0;
            GPIO.ClearInterrupt(EmulServInterruptNumber);
            return RetVal;
        }

    default:
        TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE); 
        break;
    }
}

void IOEmulServ::WriteWord(unsigned __int32 IOAddress, unsigned __int32 Value)
{
    switch (IOAddress) {
    case 0:
        InterruptMask = Value;
        if (InterruptMask & InterruptPending) {
            GPIO.RaiseInterrupt(EmulServInterruptNumber);
        }
        break;

    case 4: 
        // InterruptPending register is read-only
        break;

    default:
        TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE); 
        break;
    }
}

// This must be called with the IOLock held
void IOEmulServ::RaiseInterrupt(unsigned __int32 InterruptValue)
{
    ASSERT_CRITSEC_OWNED(IOLock);

    InterruptPending |= InterruptValue;

    if (InterruptMask & InterruptPending) {
        GPIO.RaiseInterrupt(EmulServInterruptNumber);
    }
}

