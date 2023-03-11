/*++

 Copyright (c) 2006 Microsoft Corporation.  All rights reserved.

 The use and distribution terms for this software are contained in the file
 named license.rtf, which can be found in the root of this distribution.
 By using this software in any fashion, you are agreeing to be bound by the
 terms of this license.

 You must not remove this notice, or any other, from this software.

--*/

#ifndef STATE_H_INCLUDED
#define STATE_H_INCLUDED

#include "emulator.h"

__int32 Int32FromString(const char* str);

// Functions called from the main emulator routines
class StateFiler
{
public:
	StateFiler() { succeeded = false; delete_saved_state = false; }
	void Save();
	void Restore(EmulatorConfig * PrivateConfiguration = NULL);

	// Helper functions called by serialization/deserialization code

	template<class T> void Write(const T& v)
	{
		succeeded = Write(hFile, static_cast<LPCVOID>(&v), sizeof(T));
	}

	void Write(LPCVOID p,size_t n)
	{
		succeeded = Write(hFile, p, n );
	}

	bool Write(HANDLE fileHandle, LPCVOID p,size_t n)
	{
		if (!succeeded)
			return false;
		DWORD NumberOfBytesWritten;
		if (WriteFile(fileHandle,p,static_cast<DWORD>(n),&NumberOfBytesWritten,NULL)==0)
			return false;
		if (NumberOfBytesWritten!=n)
			return false;
		return true;
	}

	template<class T> void Read(T& v)
	{
		succeeded = Read(hFile, static_cast<LPVOID>(&v),sizeof(T));
	}

	void Read(LPVOID p,size_t n)
	{
		succeeded = Read(hFile, p, n );
	}

	bool Read(HANDLE fileHandle, LPVOID p,size_t n)
	{
		if (!succeeded)
			return false;
		DWORD NumberOfBytesRead;
		if (ReadFile(fileHandle,p,static_cast<DWORD>(n),&NumberOfBytesRead,NULL)==0)
			return false;
		if (NumberOfBytesRead!=n)
			return false;
		return true;
	}

	template<class T> void Verify(const T& v)
	{
		if (!succeeded)
			return;
		T v2;
		Read(v2);
		if (v2!=v)
		{
			succeeded=false;
			ASSERT( false && "Mismatch detected in the save state file");
		}
	}

	void LZWrite(unsigned __int8* data, size_t length)
	{
		succeeded = LZWrite(data, length, hFile);
	}

	void LZRead(unsigned __int8* data, size_t length)
	{
		succeeded = LZRead(data, length, hFile);
	}

	bool LZWrite(unsigned __int8* data,size_t length, HANDLE fileHandle );
	bool LZRead(unsigned __int8* data,size_t length, HANDLE fileHandle );
	bool LZRead(unsigned __int8* data,size_t length, unsigned __int8 * input );

    void WriteString(__in_z const wchar_t *data)
    {
        unsigned __int32 StringSize;

        if (data) {
            size_t s = (wcslen(data)+1)*sizeof(wchar_t);

            if (s > 0x7fffffff) {
                goto WriteNull; // string is huge - don't even try to write it
            }
            StringSize = (unsigned __int32)s;
            Write(StringSize);
            Write(reinterpret_cast<const unsigned __int8*>(data),StringSize); // write the string, including the NULL terminator
        } else { // support WriteString(NULL), where ReadString() will return NULL
WriteNull:
            StringSize = 0xffffffff;
            Write(StringSize);
        }
    }

    void ReadString(__deref_out wchar_t *&data)
    {
        unsigned __int32 StringSize;

        Read(StringSize);
        if (StringSize == 0xffffffff) {
            data=NULL;
            return;
        }
        data = (wchar_t *)malloc(StringSize);
        if (!data) {
            succeeded = false;
            return;
        }
        Read(data, StringSize);
    }

	// May later want to make these user-controllable
	bool ForceResume() { return true; }
	bool ForceReboot() { return false; }

	// Expose status to the users of the class
	void setStatus(bool input) { succeeded = input; }
	bool getStatus() { return succeeded; }

	// Expose status to the users of the class
	unsigned __int32 getVersion() { return FileStateVersion; }
private:
	void SaveVersion();
	void RestoreVersion();

	// The "Directory" is currently just a 256-byte zero-filled pad that follows the
	// .dess file version information.  It can be used in the future as a place to
	// store extra state as part of a bug fix, to avoid changing the .dess version
	// number, or as a place to store file offsets of optional extras like a thumbnail
	// of the video frame buffer.
	static const int DirectorySize=256;
	void SaveDirectory();
	void RestoreDirectory();

	bool succeeded;
	bool delete_saved_state;
	__int32 FileStateVersion;
	HANDLE hFile;
};

#endif // STATE_H_INCLUDED

