/*++

 Copyright (c) 2006 Microsoft Corporation.  All rights reserved.

 The use and distribution terms for this software are contained in the file
 named license.rtf, which can be found in the root of this distribution.
 By using this software in any fashion, you are agreeing to be bound by the
 terms of this license.

 You must not remove this notice, or any other, from this software.

--*/

#include "emulator.h"
#include "Config.h"
#include "state.h"
#include "resource.h"
#include "loadbin_nb0.h"
#include "Board.h"

unsigned __int32 ROMHDRAddress;
ROMHDR *pROMHDR;


struct CESectionHeader
{
	// A CE container is broken into sections. Each section
	// has its own base address and length. And each section
	// begins with a 12-byte structure defined here.
	
	unsigned __int32			fSectionBaseAddress;		// Physical address where section is supposed to be loaded
	unsigned __int32			fSectionSize;				// Total bytes in section
	unsigned __int32			fSectionCheckSum;			// Checksum of bytes in section
};

bool safe_copy( void * dest, void * src, unsigned __int32 size)
{
	bool result = true;
	__try
	{
		memcpy(dest, src, size);
	}
	__except(GetExceptionCode()==EXCEPTION_IN_PAGE_ERROR ?
		EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
	{
		// Failed to read from the view. This is most likely caused by a network problem
		result = false;
	}

	return result;
}

// An NB0 file is a contiguous sequence of instructions.
bool LoadNB0File(unsigned __int8 *RomImage, DWORD FileSize) {
	size_t HostEffectiveAddress, HostEffectiveAddressEnd;
	size_t HostAdjustStart;
	size_t HostAdjustEnd;

	// WinCE 4.2 xip.nb0 should use ROMBaseAddress of (0x80041000 - NK_BIN_OFFSET + FLASH_MEMORY_BASE) = 0x32041000
	// eboot.nb0         should use ROMBaseAddress of (PHYSICAL_MEMORY_BASE+0x38000) = 0x30038000
	HostEffectiveAddress = BoardMapGuestPhysicalToHost(Configuration.ROMBaseAddress, &HostAdjustStart);
	HostEffectiveAddressEnd = BoardMapGuestPhysicalToHost( Configuration.ROMBaseAddress + FileSize - 1, &HostAdjustEnd);
	if (HostEffectiveAddress == 0 ) {
		ShowDialog(ID_MESSAGE_INVALID_ROM_ADDRESS, Configuration.ROMBaseAddress);
		return false; 
	}
	
	if (HostEffectiveAddressEnd == 0 || Configuration.ROMBaseAddress > Configuration.ROMBaseAddress + FileSize - 1) {
		ShowDialog(ID_MESSAGE_INVALID_ROM_IMAGE, Configuration.ROMBaseAddress);
		return false; 
	}

	if (HostAdjustStart != HostAdjustEnd) {
		ShowDialog(ID_MESSAGE_INVALID_ROM_IMAGE, Configuration.ROMBaseAddress);
		return false; 
	}

	if ( !safe_copy((void*)HostEffectiveAddress, RomImage, FileSize))
	{
		// Failed to read from the view. This is most likely caused by a network problem
		return false;
	}

	Configuration.InitialInstructionPointer = Configuration.ROMBaseAddress;

	return true;
}

typedef enum {
    ContinueEnumerating,
    Error_StopEnumerating,
    Success_StopEnumerating
} HeaderCallbackCode;
    
typedef HeaderCallbackCode (*pfnSectionHeaderCallback)(unsigned __int8 *RomImage, DWORD FileSize, const CESectionHeader *pSection);

bool ForEachSectionHeader(unsigned __int8 *RomImage, DWORD FileSize, pfnSectionHeaderCallback pfn)
{
	unsigned __int32 EOF_Address = PtrToLong(RomImage) + FileSize;
	CESectionHeader sectionHeaderLocal;
	void * addressSection = &RomImage[15];
	unsigned __int8 * sectionContents = ((unsigned __int8 *)addressSection) + sizeof(CESectionHeader);

	if ( !safe_copy((void*)&sectionHeaderLocal, addressSection, sizeof(CESectionHeader) ))
	{
		// Failed to read from the view. This is most likely caused by a network problem
		return false;
	}

	while (sectionHeaderLocal.fSectionBaseAddress) {
		if( (PtrToLong(sectionContents) + sectionHeaderLocal.fSectionSize) > EOF_Address ||
			(PtrToLong(sectionContents) + sectionHeaderLocal.fSectionSize) < (unsigned)PtrToLong(sectionContents) ){
			return false;
		}

		HeaderCallbackCode cc = pfn(RomImage, FileSize, (CESectionHeader *)addressSection);
		if (cc == Success_StopEnumerating) {
			break;
		} else if (cc == Error_StopEnumerating) {
			return false;
		} // else cc == ContinueEnumerating

		#pragma prefast(suppress:22008, "Prefast doesn't understand that the check below is on &sectionHeader[1]) + sectionHeader->fSectionSize")
		addressSection = (sectionContents + sectionHeaderLocal.fSectionSize);
		if ( !safe_copy((void*)&sectionHeaderLocal, addressSection, sizeof(CESectionHeader) ))
		{
			// Failed to read from the view. This is most likely caused by a network problem
			return false;
		}
		sectionContents = ((unsigned __int8 *)addressSection) + sizeof(CESectionHeader);
	}

	Configuration.InitialInstructionPointer=BoardMapVAToPA(pROMHDR, sectionHeaderLocal.fSectionSize);

	return true;
}

// Search for the section whose base address is ROMHDRAddress and confirm
// that it is a ROMHDR struct.  After enumeration, pROMHDR will point to
// the ROMHDR struct, or will be NULL if no valid ROMHDR is found.
HeaderCallbackCode FindROMHDRFromAddress(unsigned __int8 *RomImage, DWORD FileSize, const CESectionHeader *pSection)
{
	CESectionHeader sectionHeaderLocal;
	if ( !safe_copy((void*)&sectionHeaderLocal, (void *)pSection, sizeof(CESectionHeader) ))
	{
		return Error_StopEnumerating;
	}

	if (sectionHeaderLocal.fSectionBaseAddress != ROMHDRAddress || sectionHeaderLocal.fSectionSize != sizeof(ROMHDR)) {
		// The section isn't a candidate ROMHDR structure
		return ContinueEnumerating;
	}

	ROMHDR ROMHDRLocal;
	pROMHDR = (ROMHDR*)&pSection[1];
	if ( !safe_copy((void*)&ROMHDRLocal, (void *)pROMHDR, sizeof(ROMHDR) ))
	{
		return Error_StopEnumerating;
	}

	if (ROMHDRLocal.dllfirst > ROMHDRLocal.dlllast || 
		ROMHDRLocal.physfirst > ROMHDRLocal.physlast) {
		// Invalid ROMHDR structure
		pROMHDR = NULL;
	}
	return Success_StopEnumerating;
}

// Search for a valid ROMHDR.  This callback looks for sections that
// are 8 bytes long and that contain a "CECE" signature.  It then
// enumerates all sections looking for one whose offset matches the
// offset following the "CECE" signature.  That will be the ROMHDR
// itself.
HeaderCallbackCode FindROMHDR(unsigned __int8 *RomImage, DWORD FileSize, const CESectionHeader *pSection)
{
	CESectionHeader sectionHeaderLocal;
	if ( !safe_copy((void*)&sectionHeaderLocal, (void *)pSection, sizeof(CESectionHeader) ))
	{
		return Error_StopEnumerating;
	}

	if (sectionHeaderLocal.fSectionSize != 8) {
		return ContinueEnumerating;
	}

	unsigned __int32 ContentsLocal[2];
	if ( !safe_copy((void*)&ContentsLocal[0], (void *)&pSection[1], sizeof(ContentsLocal) ))
	{
		return Error_StopEnumerating;
	}

	if (ContentsLocal[0] != 0x43454345) {
		return ContinueEnumerating;
	}

	ROMHDRAddress = ContentsLocal[1];
	pROMHDR = NULL;

	if (ForEachSectionHeader(RomImage, FileSize, FindROMHDRFromAddress)) {
		// Enumerating completed successfully.  Now... did we find a ROMHDR for our pSection?
		if (pROMHDR) {
			return Success_StopEnumerating;
		} else {
			return ContinueEnumerating;
		}
	}
	return Error_StopEnumerating;
}

HeaderCallbackCode LoadSection(unsigned __int8 *RomImage, DWORD FileSize, const CESectionHeader *pSection)
{
	CESectionHeader sectionLocal;
	unsigned __int32 GuestSectionBaseAddress;
	size_t HostEffectiveAddress;
	size_t HostEffectiveAddressSectionEnd;
	size_t HostAdjust;

	if ( !safe_copy((void*)&sectionLocal, (void *)pSection, sizeof(CESectionHeader) ))
	{
		return Error_StopEnumerating;
	}

	GuestSectionBaseAddress = BoardMapVAToPA(pROMHDR, sectionLocal.fSectionBaseAddress);

	HostEffectiveAddress = BoardMapGuestPhysicalToHost(GuestSectionBaseAddress, &HostAdjust);
	HostEffectiveAddressSectionEnd = BoardMapGuestPhysicalToHost(
							GuestSectionBaseAddress + sectionLocal.fSectionSize, &HostAdjust);
	if (HostEffectiveAddress == 0 || HostEffectiveAddressSectionEnd == 0 ||
		GuestSectionBaseAddress > GuestSectionBaseAddress + sectionLocal.fSectionSize) {
		return Error_StopEnumerating;
	}

	if ( !safe_copy((void*)HostEffectiveAddress, (void*)&pSection[1], sectionLocal.fSectionSize))
	{
		// Failed to read from the view. This is most likely caused by a network problem
		return Error_StopEnumerating;
	}

	return ContinueEnumerating;
}


bool LoadBINFile(unsigned __int8 *RomImage, DWORD FileSize)
{
	// Find the ROMHDR structure in the image
	if (!ForEachSectionHeader(RomImage, FileSize, FindROMHDR)) {
		ShowDialog(ID_MESSAGE_INVALID_ROM_IMAGE);
		UnmapViewOfFile(RomImage);
		return false;
	}

	// Now load the image into emulator RAM
	if (!ForEachSectionHeader(RomImage, FileSize, LoadSection)) {
		ShowDialog(ID_MESSAGE_INVALID_ROM_IMAGE);
		UnmapViewOfFile(RomImage);
		return false;
	}

	UnmapViewOfFile(RomImage);
	return true;
}

bool Load_BIN_NB0_File(const wchar_t *RomImageFile)
{
	static const char Signature[] = "B000FF\n";
	HANDLE hFile;
	HANDLE hMapping;
	unsigned __int8 *RomImage;
	DWORD FileSize;

	hFile = CreateFile(RomImageFile, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	if (hFile == INVALID_HANDLE_VALUE) {
		ShowDialog(ID_MESSAGE_UNABLE_TO_OPEN, RomImageFile);
		return false;
	}
    
	DWORD FileSizeHigh;
	FileSize = GetFileSize(hFile, &FileSizeHigh);

	if((FileSize == 0xFFFFFFFF) && (GetLastError() != NO_ERROR )) { 
		ShowDialog(ID_MESSAGE_INVALID_ROM_IMAGE);
		return false;
	}

	if(FileSize == 0 || FileSizeHigh != 0) {
		ShowDialog(ID_MESSAGE_INVALID_ROM_IMAGE);
		return false;
	}

	hMapping = CreateFileMapping(hFile, NULL, PAGE_READONLY, 0, 0, NULL);
	CloseHandle(hFile);
	if (hMapping == INVALID_HANDLE_VALUE) {
		ShowDialog(ID_MESSAGE_INTERNAL_ERROR);
		return false;
	}

	RomImage = (unsigned __int8*)MapViewOfFile(hMapping, FILE_MAP_READ, 0, 0, 0);
	CloseHandle(hMapping);
	if (!RomImage) {
		ShowDialog(ID_MESSAGE_INTERNAL_ERROR);
		return false;
	}

	unsigned __int8 SigLocal[8];
	if ( !safe_copy( (void *)&SigLocal[0], RomImage, ARRAY_SIZE(SigLocal)) ||
		  sizeof(SigLocal) < sizeof(Signature) ||
		  FileSize < sizeof(SigLocal))
	{
		return false;
	}

	if (memcmp(SigLocal, Signature, sizeof(Signature)-1) == 0) {
		// BIN file
		// starts with B000FF
		return LoadBINFile(RomImage,FileSize);
	} else if((*((int *)&SigLocal[0]) & 0xea000000) == 0xea000000){
		// NB0 file
		// assumption: always starts with a branch instruction 0xeaXXXXXX
		return LoadNB0File(RomImage, FileSize);
	}

	ShowDialog(ID_MESSAGE_INVALID_ROM_IMAGE_TYPE);
	return false; 
}
