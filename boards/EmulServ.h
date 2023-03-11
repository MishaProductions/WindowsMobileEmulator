/*++

 Copyright (c) 2006 Microsoft Corporation.  All rights reserved.

 The use and distribution terms for this software are contained in the file
 named license.rtf, which can be found in the root of this distribution.
 By using this software in any fashion, you are agreeing to be bound by the
 terms of this license.

 You must not remove this notice, or any other, from this software.

--*/

#ifndef EMULSERV_H__
#define EMULSERV_H__

#define EmulServ_FolderSharingChanged 0x40000000 // this value must match smdk2410\device\emulserv.cpp's kFServerChangedServiceMask value

class IOEmulServ : public MappedIODevice {
public:
	virtual void __fastcall SaveState(StateFiler& filer) const;
	virtual void __fastcall RestoreState(StateFiler& filer);
	virtual unsigned __int32 __fastcall ReadWord(unsigned __int32 IOAddress);
	virtual void __fastcall WriteWord(unsigned __int32 IOAddress, unsigned __int32 Value);

    void RaiseInterrupt(unsigned __int32 InterruptValue);

private:
    unsigned __int32 InterruptMask;
    unsigned __int32 InterruptPending;
};

#endif //EMULSERV_H__
