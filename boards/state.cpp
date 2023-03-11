/*++

 Copyright (c) 2006 Microsoft Corporation.  All rights reserved.

 The use and distribution terms for this software are contained in the file
 named license.rtf, which can be found in the root of this distribution.
 By using this software in any fashion, you are agreeing to be bound by the
 terms of this license.

 You must not remove this notice, or any other, from this software.

--*/

#include "State.h"
#include "Config.h"
#include "heap.h"

// State save and restore code

#if !FEATURE_SAVESTATE
void StateFiler::SaveVersion()
{
}

void StateFiler::RestoreVersion()
{
}

void StateFiler::Save()
{
	ASSERT(FALSE);
}

void StateFiler::Restore(EmulatorConfig * PrivateConfiguration)
{
}

#else //FEATURE_SAVESTATE

#include "resource.h"
#include "Board.h"
#include "MappedIO.h"
#include "..\..\features\zlib\ZLib.h"

static const __int32 StateSig='SSED'; // Device Emulator Saved State
static const __int32 StateVersion=12;  // Increase this every time the state file changes incompatibly
static const __int32 MinimumSupportedStateVersion=11;  // Increase this when we stop supporting older format

void StateFiler::SaveVersion()
{
	Write(StateSig);
	Write(StateVersion);
	Write(StatePlatform);
}

void StateFiler::RestoreVersion()
{
	Verify(StateSig);
	if (!succeeded)
	{
		ShowDialog(ID_MESSAGE_INVALID_SAVED_FILE);
		return;
	}
	Read(FileStateVersion);
	if (!succeeded || FileStateVersion < MinimumSupportedStateVersion)
	{
		if ( !Configuration.UseDefaultSaveState ) {
			ShowDialog(ID_MESSAGE_INVALID_SAVED_FILE_VERSION);
		} else {
			if (PromptToAllow(ID_MESSAGE_INVALID_SAVED_FILE_VERSION_DELETE_OPTION, Configuration.getSaveStateFileName() ))
			{
				delete_saved_state = true;
			}
		}
		return;
	}
	Verify(StatePlatform);
	if (!succeeded)
		ShowDialog(ID_MESSAGE_INVALID_SAVED_FILE_PLATFORM);
}

void StateFiler::SaveDirectory(void)
{
	char DirectoryBuffer[DirectorySize];

	memset(DirectoryBuffer, 0, sizeof(DirectoryBuffer));
	Write(DirectoryBuffer);
}

void StateFiler::RestoreDirectory(void)
{
	char DirectoryBuffer[DirectorySize];

	Read(DirectoryBuffer);
}

class arrayowner
{
public:
	arrayowner(size_t n) : p(new unsigned __int8[n]) { }
	~arrayowner() { if (p!=0) delete[] p; }
    operator unsigned __int8*() { return p; }
private:
	unsigned __int8* p;
};

bool StateFiler::LZWrite(unsigned __int8* data,size_t length, HANDLE fileHandle)
{
	// Allow override of the succeeded flag if the function is called for a different file
	if (!succeeded && fileHandle == hFile)
		return false;

	// Allocate a buffer the same length as the input stream
	// Technically the compressed file can be as much as 0.1% plus 12 bytes larger than the original.
	// If that happens we'll just save the uncompressed memory.
	arrayowner compressed(length);
	if (compressed==0)
		return false;

	unsigned long clen=static_cast<unsigned long>(length);
	int r=compress2(compressed,&clen,data,static_cast<unsigned long>(length),1);
	if (r!=Z_OK && r!=Z_BUF_ERROR)
		return false;

	__int32 memsig='ZMEM';
	unsigned __int8* p=compressed;
	if (r==Z_BUF_ERROR) {
		// Our compressed memory image was actually longer than the original! So just save the uncompressed image.
		// This will probably never happen in real life.
		memsig='UMEM';
		clen=static_cast<unsigned long>(length);
		p=data;
	}

	// Write the output to the file
	bool status = 
		(Write(fileHandle, &memsig, sizeof(memsig)) &&
		Write(fileHandle, &clen, sizeof(clen)) &&
		Write(fileHandle, p, clen));

	return status;
}

bool StateFiler::LZRead(unsigned __int8* data,size_t length, HANDLE fileHandle)
{
	__int32 memsig = 0;
	unsigned long clen = 0;
	bool status = (Read(fileHandle, &memsig, sizeof(memsig)) && Read(fileHandle, &clen, sizeof(clen) ));
	if (!status)
		return false;
	
	if (memsig!='ZMEM' && memsig!='UMEM')
		return false;

	if (memsig=='ZMEM') {
		DWORD filepos=SetFilePointer(fileHandle,0,NULL,FILE_CURRENT);
		if (filepos==INVALID_SET_FILE_POINTER)
			return false;
		HANDLE hMapping=CreateFileMapping(fileHandle, NULL, PAGE_READONLY, 0, 0, NULL);
		if (hMapping == INVALID_HANDLE_VALUE)
			return false;
		unsigned __int8* compressed = static_cast<unsigned __int8*>(MapViewOfFile(hMapping, FILE_MAP_READ, 0, 0, clen+filepos));
		CloseHandle(hMapping);
		if (compressed==0) {
			DWORD error=GetLastError();
			if (error==0)
				clen--;
			return false;
		}
		unsigned long datalen=static_cast<unsigned long>(length);
		int r=uncompress(data,&datalen,compressed+filepos,clen);
		if (UnmapViewOfFile(compressed)==0)
			return false;
		if (datalen!=length || r!=Z_OK)
			return false;
		filepos=SetFilePointer(fileHandle,clen,NULL,FILE_CURRENT);
		if (filepos==INVALID_SET_FILE_POINTER)
			return false;
		return true;
	}

	// The memory was saved uncompressed - just read it back into the buffer
	if (clen!=length)
		return false;

	return Read(fileHandle, data, length);
}

bool StateFiler::LZRead(unsigned __int8* data,size_t length, unsigned __int8 * input)
{
	// Do a sanity check on the arguments
	if ( input == NULL || data == NULL || length == 0)
		return false;

	// Read the header
	__int32 memsig = *(__int32 *)input;
	unsigned long clen = *(unsigned long *)(input + sizeof(memsig));

	if (memsig!='ZMEM' && memsig!='UMEM')
		return false;

	// Skip over the signature and size
	input = input + sizeof(memsig) + sizeof(clen);

	// If data is compressed, uncompress it into the buffer
	if (memsig == 'ZMEM') {
		unsigned long datalen=static_cast<unsigned long>(length);
		int r=uncompress(data, &datalen, input, clen);
		if (datalen != length || r != Z_OK)
			return false;
		return true;
	}

	// The memory was saved uncompressed - just copy it back into the buffer
	if (clen!=length)
		return false;

	memcpy( data, input, length);
	return true;
}

void StateFiler::Save()
{
    wchar_t *SavedStateFileName;

    if (!Configuration.isSaveStateEnabled())
        return;

    // Make a name for the temporary file by adding ".tmp" on the end
    SavedStateFileName = Configuration.getSaveStateFileName();
    wchar_t TempFileName[MAX_PATH];
    if (wcslen(SavedStateFileName)>MAX_PATH-5) {
        SavedStateFileName[MAX_PATH-5]=0;
    }
    if (FAILED(StringCchPrintfW(TempFileName, ARRAY_SIZE(TempFileName), L"%s.tmp", SavedStateFileName))) {
        ASSERT(FALSE);
    }

	hFile = CreateFile(TempFileName,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
	succeeded=(hFile!=INVALID_HANDLE_VALUE);

	if (succeeded) {

		EnterCriticalSection(&IOLock);
		SaveVersion();
		SaveDirectory();
		Configuration.SaveState(*this);
		BoardSaveState(*this);
		SaveDeviceStates(*this);
		LeaveCriticalSection(&IOLock);

		CloseHandle(hFile);
		if (succeeded) {
			// Replace the old saved state with the new one
			succeeded=(CopyFile(TempFileName,SavedStateFileName,FALSE)!=0);
		}	
		// Delete the (possibly aborted) temporary file
		DeleteFile(TempFileName);
	}

	if (!succeeded) {
		ShowDialog(ID_MESSAGE_FAILED_SAVED_FILE_WRITE);
		exit(1);
	}
}

void StateFiler::Restore(EmulatorConfig * PrivateConfiguration)
{
    if (!Configuration.isSaveStateEnabled()) {
        succeeded = true;
        return;
    }

	hFile = CreateFile(Configuration.getSaveStateFileName(),GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
	if (hFile==INVALID_HANDLE_VALUE) {
		ShowDialog(ID_MESSAGE_UNABLE_TO_OPEN, Configuration.getSaveStateFileName());
		succeeded = false;
		return;
	}
	
	succeeded=true;
	EnterCriticalSection(&IOLock);
	RestoreVersion();
	// Exit early - we have already displayed the appropriate error message
	if ( !succeeded )
		goto Exit;
	RestoreDirectory();
	if (PrivateConfiguration == NULL)
	{
		Configuration.RestoreState(*this);
		BoardRestoreState(*this);
		RestoreDeviceStates(*this);
	}
	else
		PrivateConfiguration->RestoreState(*this);
	LeaveCriticalSection(&IOLock);

	if (!succeeded && !delete_saved_state) {
		ShowDialog(ID_MESSAGE_FAILED_SAVED_FILE_READ);
	}

Exit:
	CloseHandle(hFile);
	if (delete_saved_state) {
		DeleteFileW(Configuration.getSaveStateFileName());
	}
	// Don't exit if errors are surpressed
	if (!succeeded && !Configuration.SurpressMessages) {
		exit(1);
	}
}

#endif //FEATURE_SAVESTATE
