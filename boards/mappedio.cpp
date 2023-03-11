/*++

 Copyright (c) 2006 Microsoft Corporation.  All rights reserved.

 The use and distribution terms for this software are contained in the file
 named license.rtf, which can be found in the root of this distribution.
 By using this software in any fashion, you are agreeing to be bound by the
 terms of this license.

 You must not remove this notice, or any other, from this software.

--*/

#include "emulator.h"
#include "config.h"
#include "MappedIO.h"
#include "Board.h"
#include <search.h>
#include "devices.h"

// Include the logging infrastructure
#include "dev_emulator_log.h"
#define TMH_FILENAME "mappedio.tmh"
#include "vsd_logging_inc.h"

typedef struct {
	unsigned __int32 StartAddress;
	unsigned __int32 EndAddress;
	MappedIODevice *Device;
	char * name;
} MappedIORange;

#define MAPPEDIODEVICE(baseclass, devicename, iobase, iolength) \
	{devicename##_IOBase, devicename##_IOEnd, &devicename, #baseclass},

const MappedIORange IORanges[] = {
	#include "mappediodevices.h"
};

#undef MAPPEDIODEVICE

bool __fastcall PowerOnDevices()
{
	int i;

	// Ensure that the IORanges list is in sorted order
	#pragma prefast(suppress:294, "Prefast does not understand for loop condition due to ARRAY_SIZE call")
	for (i=1; i < ARRAY_SIZE(IORanges); ++i) {
		ASSERT(IORanges[i].StartAddress > IORanges[i-1].StartAddress);
	}

	// Power on each device in turn
	for (i=0; i < ARRAY_SIZE(IORanges); ++i) {
		if(!IORanges[i].Device->PowerOn()) {
			return false;
		}
	}
	return true;
}

bool __fastcall ResetDevices()
{
	int i;

	// Ensure that the IORanges list is in sorted order
	#pragma prefast(suppress:294, "Prefast is confused by the ARRAY_SIZE call in condition")
	for (i=1; i < ARRAY_SIZE(IORanges); ++i) {
		ASSERT(IORanges[i].StartAddress > IORanges[i-1].StartAddress);
	}

	// Reset devices in turn
	for (i=0; i < ARRAY_SIZE(IORanges); ++i) {
		if(!IORanges[i].Device->Reset()) {
			return false;
		}
	}
	return true;
}

void __fastcall SaveDeviceStates(StateFiler& filer)
{
	// Save state for each device in turn
	for (int i=0; i < ARRAY_SIZE(IORanges); ++i) {
		IORanges[i].Device->SaveState(filer);
	}
}

void __fastcall RestoreDeviceStates(StateFiler& filer)
{
	// Restore state for each device in turn
	for (int i=0; i < ARRAY_SIZE(IORanges) && filer.getStatus(); ++i) {
		IORanges[i].Device->RestoreState(filer);
	}
}
 
class IODefaultDevice DefaultDevice;
class IOPCMCIADefaultDevice PCMCIADefaultDevice;

const MappedIORange UnmappedIO = { 0x00000000, 0x00000000, &DefaultDevice};

int __cdecl CompareIORange ( const void *element, const void *entry)
{
	unsigned __int32 IOAddress = *(unsigned __int32 *)element;
	MappedIORange *Device = (MappedIORange *)entry;

	if (IOAddress < Device->StartAddress) {
		return -1;
	} else if (IOAddress > Device->EndAddress) {
		return 1;
	}
	return 0;
}

bool isIODeviceAddressCached (const MappedIORange *Device, unsigned __int32 boardAddress)
{
	if (boardAddress < Device->StartAddress) {
		return false;
	} else if (boardAddress > Device->EndAddress) {
		return false;
	}
	return true;
}

const MappedIORange * __fastcall FindIORange(unsigned __int32 IOAddress)
{
	MappedIORange *Device;

	LogPrint((output, "Attempting I/O at address 0x%8.8x\n", IOAddress));
	Device = (MappedIORange *)bsearch(&IOAddress, IORanges, ARRAY_SIZE(IORanges), sizeof(IORanges[0]), CompareIORange);
	if (Device == NULL || Device->EndAddress < IOAddress) {
		return &UnmappedIO;
	}
	return Device;
}

unsigned __int8 __fastcall IOReadByte(__int8 *pIOIndexHint)
{
	MappedIORange *pIORange;
	unsigned __int8 Ret;
	unsigned __int32 IOAddress = BoardIOAddress;
	__int8 IODeviceIndex;

	IODeviceIndex = *pIOIndexHint;
	pIORange = (MappedIORange*)(&(IORanges[IODeviceIndex]));

	if (!isIODeviceAddressCached(pIORange, BoardIOAddress))
	{	
		pIORange = (MappedIORange*)FindIORange(IOAddress);
		IODeviceIndex = (__int8 )((PtrToLong(pIORange) - PtrToLong(IORanges)) / sizeof(IORanges[0]));
		*pIOIndexHint = IODeviceIndex;
	}
	
	EnterCriticalSection(&IOLock);
	Ret = pIORange->Device->ReadByte(IOAddress-pIORange->StartAddress);
	LeaveCriticalSection(&IOLock);
#if defined(LOGGING_ENABLED) || defined(_DEBUG)
	LOG_VVERBOSE(PERIPHERAL_CALLS,"Read BYTE from %s address %x - value %x", pIORange->name, 
				IOAddress-pIORange->StartAddress, Ret);
#endif
	return Ret;
}

unsigned __int16 __fastcall IOReadHalf(__int8 *pIOIndexHint)
{
	MappedIORange *pIORange;
	unsigned __int16 Ret;
	unsigned __int32 IOAddress = BoardIOAddress;
	__int8 IODeviceIndex;

	IODeviceIndex = *pIOIndexHint;
	pIORange = (MappedIORange*)(&(IORanges[IODeviceIndex]));

	if (!isIODeviceAddressCached(pIORange, BoardIOAddress))
	{	
		pIORange = (MappedIORange*)FindIORange(IOAddress);
		IODeviceIndex = (__int8 )((PtrToLong(pIORange) - PtrToLong(IORanges)) / sizeof(IORanges[0]));
		*pIOIndexHint = IODeviceIndex;
	}
	

	EnterCriticalSection(&IOLock);
	Ret = pIORange->Device->ReadHalf(IOAddress-pIORange->StartAddress);
	LeaveCriticalSection(&IOLock);

#if defined(LOGGING_ENABLED) || defined(_DEBUG)
	LOG_VVERBOSE(PERIPHERAL_CALLS,"Read HALF from %s address %x - value %x", pIORange->name, 
				IOAddress-pIORange->StartAddress, Ret);
#endif

	return Ret;
}

unsigned __int32 __fastcall IOReadWord(__int8 *pIOIndexHint)
{
	MappedIORange *pIORange;
	unsigned __int32 Ret;
	unsigned __int32 IOAddress = BoardIOAddress;
	__int8 IODeviceIndex;

	IODeviceIndex = *pIOIndexHint;
	pIORange = (MappedIORange*)(&(IORanges[IODeviceIndex]));

	if (!isIODeviceAddressCached(pIORange, BoardIOAddress))
	{	
		pIORange = (MappedIORange*)FindIORange(IOAddress);
		IODeviceIndex = (__int8 )((PtrToLong(pIORange) - PtrToLong(IORanges)) / sizeof(IORanges[0]));
		*pIOIndexHint = IODeviceIndex;
	}
	
	EnterCriticalSection(&IOLock);
	Ret= pIORange->Device->ReadWord(IOAddress-pIORange->StartAddress);
	LeaveCriticalSection(&IOLock);

#if defined(LOGGING_ENABLED) || defined(_DEBUG)
	LOG_VVERBOSE(PERIPHERAL_CALLS,"Read WORD from %s address %x - value %x", pIORange->name, 
				IOAddress-pIORange->StartAddress, Ret);
#endif

	return Ret;
}

void __fastcall IOWriteByte(unsigned __int8  Value, __int8 *pIOIndexHint)
{
	MappedIORange *pIORange;
	unsigned __int32 IOAddress = BoardIOAddress;
	__int8 IODeviceIndex;

	IODeviceIndex = *pIOIndexHint;
	pIORange = (MappedIORange*)(&(IORanges[IODeviceIndex]));

	if (!isIODeviceAddressCached(pIORange, BoardIOAddress))
	{	
		pIORange = (MappedIORange*)FindIORange(IOAddress);
		IODeviceIndex = (__int8 )((PtrToLong(pIORange) - PtrToLong(IORanges)) / sizeof(IORanges[0]));
		*pIOIndexHint = IODeviceIndex;
	}
	
	EnterCriticalSection(&IOLock);
	pIORange->Device->WriteByte(IOAddress-pIORange->StartAddress, Value);
	LeaveCriticalSection(&IOLock);

#if defined(LOGGING_ENABLED) || defined(_DEBUG)
	LOG_VVERBOSE(PERIPHERAL_CALLS,"Wrote BYTE to %s address %x - value %x", pIORange->name, 
				IOAddress-pIORange->StartAddress, Value);
#endif
}

void __fastcall IOWriteHalf(unsigned __int16 Value, __int8 *pIOIndexHint)
{
	MappedIORange *pIORange;
	unsigned __int32 IOAddress = BoardIOAddress + BoardIOAddressAdj;
	__int8 IODeviceIndex;

		
	IODeviceIndex = *pIOIndexHint;
	pIORange = (MappedIORange*)(&(IORanges[IODeviceIndex]));

	if (!isIODeviceAddressCached(pIORange, BoardIOAddress))
	{	
		pIORange = (MappedIORange*)FindIORange(IOAddress);
		IODeviceIndex = (__int8 )((PtrToLong(pIORange) - PtrToLong(IORanges)) / sizeof(IORanges[0]));
		*pIOIndexHint = IODeviceIndex;
	}
	EnterCriticalSection(&IOLock);
	pIORange->Device->WriteHalf(IOAddress-pIORange->StartAddress, Value);
	LeaveCriticalSection(&IOLock);

#if defined(LOGGING_ENABLED) || defined(_DEBUG)
	LOG_VVERBOSE(PERIPHERAL_CALLS,"Wrote HALF to %s address %x - value %x", pIORange->name, 
				IOAddress-pIORange->StartAddress, Value);
#endif
}

void __fastcall IOWriteWord(unsigned __int32 Value, __int8 *pIOIndexHint)
{
	MappedIORange *pIORange;
	unsigned __int32 IOAddress = BoardIOAddress;
	__int8 IODeviceIndex;

	IODeviceIndex = *pIOIndexHint;
	pIORange = (MappedIORange*)(&(IORanges[IODeviceIndex]));
 
	if (!isIODeviceAddressCached(pIORange, BoardIOAddress))
	{	
		pIORange = (MappedIORange*)FindIORange(IOAddress);
		IODeviceIndex = (__int8 )((PtrToLong(pIORange) - PtrToLong(IORanges)) / sizeof(IORanges[0]));
		*pIOIndexHint = IODeviceIndex;
	}

	EnterCriticalSection(&IOLock);
	pIORange->Device->WriteWord(IOAddress-pIORange->StartAddress, Value);
	LeaveCriticalSection(&IOLock);

#if defined(LOGGING_ENABLED) || defined(_DEBUG)
	LOG_VVERBOSE(PERIPHERAL_CALLS,"Wrote WORD to %s address %x - value %x", pIORange->name, 
				IOAddress-pIORange->StartAddress, Value);
#endif
}

bool __fastcall MappedIODevice::PowerOn(){
	return true;
}

bool __fastcall MappedIODevice::Reset(){
	return true;
}

void __fastcall MappedIODevice::SaveState(StateFiler& filer) const
{
	// This is only overridden for devices having state which needs saving and restoring
}

void __fastcall MappedIODevice::RestoreState(StateFiler& filer)
{
	// This is only overridden for devices having state which needs saving and restoring
}


bool __fastcall MappedIODevice::Reconfigure(__in_z const wchar_t * NewParam)
{
    return true;
}

unsigned __int8  __fastcall MappedIODevice::ReadByte(unsigned __int32 IOAddress) {
	ASSERT(FALSE);
	return 0;
}

unsigned __int16 __fastcall MappedIODevice::ReadHalf(unsigned __int32 IOAddress) {
	ASSERT(FALSE);
	return 0;
}

unsigned __int32 __fastcall MappedIODevice::ReadWord(unsigned __int32 IOAddress) {
	ASSERT(FALSE);
	return 0;
}

void __fastcall MappedIODevice::WriteByte(unsigned __int32 IOAddress, unsigned __int8  Value) {
	ASSERT(FALSE);
	return;
}

void __fastcall MappedIODevice::WriteHalf(unsigned __int32 IOAddress, unsigned __int16 Value) {
	ASSERT(FALSE);
	return;
}

void __fastcall MappedIODevice::WriteWord(unsigned __int32 IOAddress, unsigned __int32 Value) {
	ASSERT(FALSE);
	return;
}

unsigned __int8  __fastcall IODefaultDevice::ReadByte(unsigned __int32 IOAddress) {
	return 0;
}

unsigned __int16 __fastcall IODefaultDevice::ReadHalf(unsigned __int32 IOAddress) {
	return 0;
}

unsigned __int32 __fastcall IODefaultDevice::ReadWord(unsigned __int32 IOAddress) {
	return 0;
}

void __fastcall IODefaultDevice::WriteByte(unsigned __int32 IOAddress, unsigned __int8  Value) {
	return;
}

void __fastcall IODefaultDevice::WriteHalf(unsigned __int32 IOAddress, unsigned __int16 Value) {
	return;
}

void __fastcall IODefaultDevice::WriteWord(unsigned __int32 IOAddress, unsigned __int32 Value) {
	return;
}

unsigned __int8  __fastcall IOPCMCIADefaultDevice::ReadByte(unsigned __int32 IOAddress) {
	return 0;
}

unsigned __int16 __fastcall IOPCMCIADefaultDevice::ReadHalf(unsigned __int32 IOAddress) {
	return 0;
}

void __fastcall IOPCMCIADefaultDevice::WriteByte(unsigned __int32 IOAddress, unsigned __int8  Value) {
	return;
}

void __fastcall IOPCMCIADefaultDevice::WriteHalf(unsigned __int32 IOAddress, unsigned __int16 Value) {
	return;
}

unsigned __int8  __fastcall IOPCMCIADefaultDevice::ReadMemoryByte(unsigned __int32 IOAddress) {
	return 0;
}

unsigned __int16 __fastcall IOPCMCIADefaultDevice::ReadMemoryHalf(unsigned __int32 IOAddress) {
	return 0;
}

void __fastcall IOPCMCIADefaultDevice::WriteMemoryByte(unsigned __int32 IOAddress, unsigned __int8  Value) {
	return;
}

void __fastcall IOPCMCIADefaultDevice::WriteMemoryHalf(unsigned __int32 IOAddress, unsigned __int16 Value) {
	return;
}
