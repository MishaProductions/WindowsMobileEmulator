/*++

 Copyright (c) 2006 Microsoft Corporation.  All rights reserved.

 The use and distribution terms for this software are contained in the file
 named license.rtf, which can be found in the root of this distribution.
 By using this software in any fashion, you are agreeing to be bound by the
 terms of this license.

 You must not remove this notice, or any other, from this software.

--*/

#ifndef FOLDERSHARING__H_
#define FOLDERSHARING__H_

class IOFolderSharing : public MappedIODevice {
public:

    IOFolderSharing();

    virtual bool __fastcall PowerOn(void);
    virtual bool __fastcall Reconfigure(__in_z const wchar_t * NewParam);
    virtual bool __fastcall Reset(void);

    virtual void __fastcall SaveState(StateFiler& filer) const;
    virtual void __fastcall RestoreState(StateFiler& filer);

    virtual unsigned __int32 __fastcall ReadWord(unsigned __int32 IOAddress);
    virtual void __fastcall WriteWord(unsigned __int32 IOAddress, unsigned __int32 Value);

private:
    typedef unsigned __int8  UInt8;
    typedef unsigned __int16 UInt16;
    typedef unsigned __int32 UInt32;
    typedef          __int16 SInt16;
    #define MYFAR
    typedef unsigned __int8 Boolean;
    typedef wchar_t         UniChar;
    #define kMaxLFN     255                 // # of characters in our long file names

    // This structure is a copy of the one in fserver.h and must be kept in sync
    typedef  struct ServerPB    {
        UInt16      fStructureSize;         // sizeof(ServerPB)
        UInt16      fResult;                // Request Result Code (error)
        UInt32      fFindTransactionID;     // see file.c
        SInt16      fIndex;                 // Which directory entry
        UInt16      fHandle;                // Open file's handle (FServer Handle)

        UInt32      fFileTimeDate;          // file's time & date
        UInt32      fSize;                  // File's Size
        UInt32      fPosition;              // File's Position
        UInt8 MYFAR *fDTAPtr;               // Disk Transfer Address
        UInt16      fFileAttributes;        // File's Attributes
        UInt16      fOpenMode;              // Open Mode
        Boolean     fWildCard;              // TRUE if Dos Name contains a wildcard

        union {
            // Note:  emulator fserver.h contains fDos and fBoth branches to the union, but they're
            //        Unused.  Preserve the union to reduce unneeded code differences between the
            //        emulator and DeviceEmulator versions of this codebase.
            struct {
                UInt16      fNameLength;        // Unicode long file name length in bytes (excludes NULL)
                UniChar     fName[kMaxLFN+1];   // Unicode long file name (NULL terminated)
                UInt16      fName2Length;       // Unicode long file name length in bytes (excludes NULL)
                UniChar     fName2[kMaxLFN+1];  // Unicode long file name (NULL terminated)
            } fLfn;

        } u;

        UInt32      fFileCreateTimeDate;

    } ServerPB, * PServerPB ;

    typedef unsigned __int32 (__fastcall *pfnAsyncRequest_t)(class IOFolderSharing *pThis, PServerPB pServerPB);

    static DWORD WINAPI StaticWorkerThread(LPVOID lpParameter);
    void WorkerThread(void);
    void SignalIOComplete(unsigned __int32 ResultValue);
    __inline bool IsFolderSharingEnabled(void) { return (hIOComplete != INVALID_HANDLE_VALUE); }
    PServerPB GetServerPB(void); // this either succeeds or calls TerminateWithMessage() to exit the process
    void AsyncCall(pfnAsyncRequest_t pfn);

    static unsigned __int32 FiletimeToLong(const FILETIME *ft);
    static bool LongToFiletime(unsigned __int32 fFileTimeDate, FILETIME *ft);
    UInt16 GetWin32Path(__out_bcount(cbWinCEPath) wchar_t *WinCEPath, size_t cbWinCEPath, __inout_ecount(cchWin32Path) wchar_t *Win32Path, size_t cchWin32Path);
    static bool DesiredAccessFromOpenMode(UInt16 OpenMode, DWORD *pdwDesiredAccess);
    static bool ShareModeFromOpenMode(UInt16 OpenMode, DWORD *pdwShareMode);
    static UInt16 GetCEFileAttributes(DWORD dwWin32FileAttributes);
    void UpdateResultsFromFind(PServerPB pServerPB, LPWIN32_FIND_DATA lpFindData, bool fWritebackName);

    void __fastcall ServerPollCompletion(void);
    void __fastcall ServerGetDriveConfig(void);
    static unsigned __int32 __fastcall AsyncServerOpen(class IOFolderSharing *pThis, PServerPB pServerPB);
    static unsigned __int32 __fastcall AsyncServerCreate(class IOFolderSharing *pThis, PServerPB pServerPB);
    static unsigned __int32 __fastcall AsyncServerSetEOF(class IOFolderSharing *pThis, PServerPB pServerPB);
    static unsigned __int32 __fastcall AsyncServerGetInfo(class IOFolderSharing *pThis, PServerPB pServerPB);
    static unsigned __int32 __fastcall AsyncServerRead(class IOFolderSharing *pThis, PServerPB pServerPB);
    static unsigned __int32 __fastcall AsyncServerWrite(class IOFolderSharing *pThis, PServerPB pServerPB);
    static unsigned __int32 __fastcall AsyncServerClose(class IOFolderSharing *pThis, PServerPB pServerPB);
    static unsigned __int32 __fastcall AsyncServerGetFCBInfo(class IOFolderSharing *pThis, PServerPB pServerPB);
    static unsigned __int32 __fastcall AsyncServerGetSpace(class IOFolderSharing *pThis, PServerPB pServerPB);
    static unsigned __int32 __fastcall AsyncServerMkDir(class IOFolderSharing *pThis, PServerPB pServerPB);
    static unsigned __int32 __fastcall AsyncServerRmDir(class IOFolderSharing *pThis, PServerPB pServerPB);
    static unsigned __int32 __fastcall AsyncServerSetAttributes(class IOFolderSharing *pThis, PServerPB pServerPB);
    static unsigned __int32 __fastcall AsyncServerRename(class IOFolderSharing *pThis, PServerPB pServerPB);
    static unsigned __int32 __fastcall AsyncServerDelete(class IOFolderSharing *pThis, PServerPB pServerPB);
    void __fastcall ServerGetMaxIOSize(void);

    // Guards FolderShareRoot, NewRoot, and fNeedsReconfiguration, fGuestReset,
    // fDontTriggerInterrupt 
    // The IOLock must be acquired before this lock, to avoid deadlock
    CRITICAL_SECTION FolderShareLock;

    wchar_t FolderShareRoot[_MAX_PATH];
    HANDLE hIOComplete;

    COMPLETIONPORT_CALLBACK Callback;
    static void CompletionRoutineStatic(LPVOID lpParameter, DWORD dwBytesTransferred, LPOVERLAPPED lpOverlapped);
    void CompletionRoutine(void);

    unsigned __int32 IOResult;
    pfnAsyncRequest_t pfnAsyncRequest;

    // Fields and methods used only to manage reconfiguration, which is run on the worker thread, async from the 
    // Reconfigure() method itself
    wchar_t *NewRoot; // New FolderShareRoot value to use
    bool fNeedsReconfiguration; // true if the worker thread needs to reconfigure
    bool fDontTriggerInterrupt; // true if we are restoring from saved state
    bool fGuestReset; // true if we are reconfiguring due to reset
    void __fastcall AsyncReconfigure(void);


    // Registers in the IOFolderSharing device
    unsigned __int32 ServerPBAddr;  // Physical address of ServerPB
    unsigned __int32 Code;
    unsigned __int32 IOPending;
    unsigned __int32 Result;
};

#endif // FOLDERSHARING__H_
