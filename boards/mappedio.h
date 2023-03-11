/*++

 Copyright (c) 2006 Microsoft Corporation.  All rights reserved.

 The use and distribution terms for this software are contained in the file
 named license.rtf, which can be found in the root of this distribution.
 By using this software in any fashion, you are agreeing to be bound by the
 terms of this license.

 You must not remove this notice, or any other, from this software.

--*/

#ifndef _MAPPEDIO__H_
#define _MAPPEDIO__H_

#include "state.h"

bool __fastcall PowerOnDevices();
bool __fastcall ResetDevices();
void __fastcall SaveDeviceStates(StateFiler& filer);
void __fastcall RestoreDeviceStates(StateFiler& filer);

// Base class for mapped-IO devices.  If a class chooses to not implement one of the
// methods, a default implementation is used, which asserts and acts as read-0/write-any.
typedef class MappedIODevice {
public:
	virtual bool __fastcall PowerOn();
	virtual bool __fastcall Reset();

	virtual void __fastcall SaveState(StateFiler& filer) const;
	virtual void __fastcall RestoreState(StateFiler& filer);
	virtual bool __fastcall Reconfigure(__in_z const wchar_t * NewParam);

	virtual unsigned __int8  __fastcall ReadByte(unsigned __int32 IOAddress);
	virtual unsigned __int16 __fastcall ReadHalf(unsigned __int32 IOAddress);
	virtual unsigned __int32 __fastcall ReadWord(unsigned __int32 IOAddress);

	virtual void __fastcall WriteByte(unsigned __int32 IOAddress, unsigned __int8  Value);
	virtual void __fastcall WriteHalf(unsigned __int32 IOAddress, unsigned __int16 Value);
	virtual void __fastcall WriteWord(unsigned __int32 IOAddress, unsigned __int32 Value);
} MappedIODevice;

typedef class PCMCIADevice : public MappedIODevice {
public:
	// The PowerOn() is not called during emulator bootup... instead, it is called
	// when the PCMCIA controller applies power to the slot.  PowerOff() is called
	// when the card is powered down or removed.
	virtual void PowerOff(void) { };

	// These methods are called for guest memory accesses pointing into a mapped memory window
	virtual unsigned __int8  __fastcall ReadMemoryByte(unsigned __int32 IOAddress)=0;
	virtual unsigned __int16 __fastcall ReadMemoryHalf(unsigned __int32 IOAddress)=0;

	virtual void __fastcall WriteMemoryByte(unsigned __int32 IOAddress, unsigned __int8  Value)=0;
	virtual void __fastcall WriteMemoryHalf(unsigned __int32 IOAddress, unsigned __int16 Value)=0;
} PCMCIADevice;

// A stub default device to be used for PCMCIA I/O when card is powered off or removed.  
// It simply logs the fact that the IO attempt happened, and reads 0 and writes any.
class IOPCMCIADefaultDevice : public PCMCIADevice {
public:
	virtual unsigned __int8  __fastcall ReadByte(unsigned __int32 IOAddress);
	virtual unsigned __int16 __fastcall ReadHalf(unsigned __int32 IOAddress);
	virtual void __fastcall WriteByte(unsigned __int32 IOAddress, unsigned __int8  Value);
	virtual void __fastcall WriteHalf(unsigned __int32 IOAddress, unsigned __int16 Value);

	virtual unsigned __int8  __fastcall ReadMemoryByte(unsigned __int32 IOAddress);
	virtual unsigned __int16 __fastcall ReadMemoryHalf(unsigned __int32 IOAddress);
	virtual void __fastcall WriteMemoryByte(unsigned __int32 IOAddress, unsigned __int8  Value);
	virtual void __fastcall WriteMemoryHalf(unsigned __int32 IOAddress, unsigned __int16 Value);
}; 

// A stub default device to be used for unknown I/O.  It simply logs the fact that the IO
// attempt happened, and reads 0 and writes any.
class IODefaultDevice : public MappedIODevice {
public:
	virtual unsigned __int8  __fastcall ReadByte(unsigned __int32 IOAddress);
	virtual unsigned __int16 __fastcall ReadHalf(unsigned __int32 IOAddress);
	virtual unsigned __int32 __fastcall ReadWord(unsigned __int32 IOAddress);

	virtual void __fastcall WriteByte(unsigned __int32 IOAddress, unsigned __int8  Value);
	virtual void __fastcall WriteHalf(unsigned __int32 IOAddress, unsigned __int16 Value);
	virtual void __fastcall WriteWord(unsigned __int32 IOAddress, unsigned __int32 Value);

};
	
extern class IODefaultDevice DefaultDevice;
extern class IOPCMCIADefaultDevice PCMCIADefaultDevice;

#endif // _MAPPEDIO__H_
