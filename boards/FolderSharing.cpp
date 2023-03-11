/*++

 Copyright (c) 2006 Microsoft Corporation.  All rights reserved.

 The use and distribution terms for this software are contained in the file
 named license.rtf, which can be found in the root of this distribution.
 By using this software in any fashion, you are agreeing to be bound by the
 terms of this license.

 You must not remove this notice, or any other, from this software.


    Implements the emulator side of Folder Sharing.  The VCEFSD driver
    communicates with the IOFolderSharing device to pass requests
    through from WinCE to Win32.

    Note that VCEFSD stands for "VirtualCE Filesystem Driver".

    IO is performed via a worker thread so slow operations like
    read/write/create don't block emulated interrupt delivery.  The
    VCEFSD driver initiates the I/O then polls a register in the
    IOFolderSharing device until the I/O completes.  The polling is
    implemented efficiently in the DeviceEmulator:  the emulator
    recognizes the poll and blocks until an interrupt really is
    pending, or until the I/O completes.

--*/

#include "emulator.h"
#include "Config.h"
#include "MappedIO.h"
#include "Board.h"
#include "resource.h"
#include "Devices.h"
#pragma warning (disable:4995) // name was marked as #pragma deprecated - triggered by shlwapi.h
#include <shlwapi.h>
#pragma warning(default:4995)

// Include the logging infrastructure
#include "dev_emulator_log.h"
#define TMH_FILENAME "foldersharing.tmh"
#include "vsd_logging_inc.h"

#define IsFolderSharingLoggingEnabled(level) (WPP_COMPID_LEVEL_ENABLED(FOLDERSHARING, LVL_ ## level))

// Note:  these values are shared with the BSP in devices\vcefsd\fserver.h
#define kServerPollCompletion 0x00      //
#define kServerInitialize   0x01        // not used by VCEFSD.  Connectix EMULSERV calls it, passing CP_ACP
#define kServerCheckConfig  0x02        // not used
#define kServerGetConfig    0x03        // not used
#define kServerGetDriveConfig 0x04      //
#define kServerCreate       0x05        //
#define kServerOpen         0x06        //
#define kServerRead         0x07        //
#define kServerWrite        0x08        //
#define kServerSetEOF       0x09        //
#define kServerClose        0x0A        //
#define kServerGetSpace     0x0B        //
#define kServerMkDir        0x0C        //
#define kServerRmDir        0x0D        //
#define kServerSetAttributes 0x0E       //
#define kServerRename       0x0F        //
#define kServerDelete       0x10        //
#define kServerGetInfo      0x11        //
#define kServerLock         0x12        // not used
#define kServerGetFCBInfo   0x13        //
#define kServerUseNotify    0x14        // not used
#define kServerGetMaxIOSize 0x15        // 
#define kMaxServerFunction  0x15        // Max Valid function number

//    Error Codes - these values come from Connectix vs\source\vm\common\devices\vpcredir.h

#define kErrorNoError               0
#define    kErrorInvalidFunction        0x0001
#define    kErrorFileNotFound            0x0002
#define    kErrorPathNotFound            0x0003
#define    kErrorTooManyOpenFiles        0x0004
#define    kErrorAccessDenied            0x0005
#define    kErrorInvalidHandle            0x0006
#define    kErrorNotSameDevice            0x0011
#define    kErrorNoMoreFiles            0x0012
#define    kErrorWriteProtect            0x0013
#define    kErrorWriteFault            0x001D
#define    kErrorReadFault                0x001E
#define    kErrorGeneralFailure        0x001F
#define    kErrorSharingViolation        0x0020
#define    kErrorLockViolation            0x0021
#define    kErrorSharingBufferExceeded 0x0024
#define    kErrorDiskFull                0x0027
#define    kErrorFileExists            0x0050
#define kErrorInvalidName            0x007B

unsigned __int16 kErrorFromLastError(void);


// From vs\source\vm\common\hgint\vpcfoldersharingdevice.h:
#define kFolderSharingMaxReadWriteSize        (1024*64)        // 64K, largest single read or write

// From vcefsd\file.c
#define    kOpenAccessReadOnly            0x0000
#define    kOpenAccessWriteOnly        0x0001
#define    kOpenAccessReadWrite        0x0002
#define    kOpenShareCompatibility        0x0000
#define    kOpenShareDenyReadWrite        0x0010
#define    kOpenShareDenyWrite            0x0020
#define    kOpenShareDenyRead            0x0030
#define    kOpenShareDenyNone            0x0040

/*
    The SharedFileHandleManager is responsible for maintaining a mapping between
    Win32 HANDLE values and UInt16 fHandle values.  For each HANDLE/fHandle
    pair, it also retains the filename and OpenMode of the file.

    The class tracks only 40 files as vcefsd\file.c gFDList[] array is used to
    track open files, and it is limited to 40 open WinCE HANDLEs concurrently.

*/
class SharedFileHandleManager {
public:
    void PowerOn(void);
    void Reset(void);
    unsigned __int16 AllocateSharedFileHandle(void);
    void SetSharedFileHandle(unsigned __int16 fHandle, HANDLE FileHandle, unsigned __int16 OpenMode, __inout_z wchar_t * FileName);
    HANDLE GetSharedFileHandle(unsigned __int16 fHandle);
    bool GetSharedFileHandleAttributes(unsigned __int16 fHandle, unsigned __int16 *pOpenMode, __deref_out_z wchar_t **pFileName);
    unsigned __int16 CloseSharedFileHandle(unsigned __int16 fHandle);

private:
#define kMaxFD 40   // maximum number of open files (from vcefsd\file.c)
#define InvalidSharedFileHandle 0xffff
    struct {
        HANDLE hFile;
        unsigned __int16 OpenMode;
        wchar_t *FileName;
    } SharedFileHandles[kMaxFD];
};

class SharedFileHandleManager SharedFileHandleManager;

void SharedFileHandleManager::PowerOn(void)
{
    for (int i=0; i<kMaxFD; ++i) {
        SharedFileHandles[i].hFile = INVALID_HANDLE_VALUE;
    }
}

void SharedFileHandleManager::Reset(void)
{
    for (int i=0; i<kMaxFD; ++i) {
        if (SharedFileHandles[i].hFile != INVALID_HANDLE_VALUE) {
            CloseHandle(SharedFileHandles[i].hFile);
            SharedFileHandles[i].hFile = INVALID_HANDLE_VALUE;

            free(SharedFileHandles[i].FileName);
            SharedFileHandles[i].FileName = NULL;
        }
    }
}

// Reserve a slot in the shared file handle mapping table.
//
// Returns:
//  InvalidSharedFileHandle if the table is full
//  other values indicate success and are the VCEFSD fHandle value
unsigned __int16 SharedFileHandleManager::AllocateSharedFileHandle(void)
{
    for (unsigned __int16 i=0; i<ARRAY_SIZE(SharedFileHandles); ++i) {
        if (SharedFileHandles[i].hFile == INVALID_HANDLE_VALUE) {
            return i;
        }
    }
    return InvalidSharedFileHandle;
}

// Maps a VCEFSD fHandle value to a Win32 HANDLE.
//
// Returns:
//  INVALID_HANDLE_VALUE if the fHandle doesn't correspond to a Win32 HANDLE
//  other values indicate success
HANDLE SharedFileHandleManager::GetSharedFileHandle(unsigned __int16 fHandle)
{
    if (fHandle >= ARRAY_SIZE(SharedFileHandles)) {
        return INVALID_HANDLE_VALUE;
    }
    return SharedFileHandles[fHandle].hFile;
}

// Retrives the attributes of an fHandle (the OpenMode and FileName)
//
// Returns:
//  false if fHandle doesn't correspond to a Win32 HANDLE
//  true (and *pOpenMode and *pFileName filled in) on success.  Note that
//        the caller must not free *pFileName:  the SharedFileManager continues
//        to own that memory allocation.
bool SharedFileHandleManager::GetSharedFileHandleAttributes(unsigned __int16 fHandle, unsigned __int16 *pOpenMode, __deref_out_z wchar_t **pFileName)
{
    if (fHandle >= ARRAY_SIZE(SharedFileHandles) || 
        SharedFileHandles[fHandle].hFile == INVALID_HANDLE_VALUE) {
        return false;
    }

    *pOpenMode = SharedFileHandles[fHandle].OpenMode;
    *pFileName = SharedFileHandles[fHandle].FileName;
    return true;
}


// Completes the association between a previously-allocated
// VCEFSD fHandle and a newly-opened Win32 file HANDLE.
void SharedFileHandleManager::SetSharedFileHandle(unsigned __int16 fHandle, HANDLE FileHandle, unsigned __int16 OpenMode, __inout_z  wchar_t * FileName)
{
    ASSERT(fHandle < ARRAY_SIZE(SharedFileHandles));
    ASSERT(SharedFileHandles[fHandle].hFile == INVALID_HANDLE_VALUE);
    ASSERT(FileHandle != INVALID_HANDLE_VALUE);

    SharedFileHandles[fHandle].hFile = FileHandle;
    SharedFileHandles[fHandle].OpenMode = OpenMode;
    SharedFileHandles[fHandle].FileName = FileName;
}

// Closes a VCEFSD fHandle and its corresponding Win32
// file HANDLE.
unsigned __int16 SharedFileHandleManager::CloseSharedFileHandle(unsigned __int16 fHandle)
{
    if (fHandle >= ARRAY_SIZE(SharedFileHandles)) {
        return kErrorInvalidHandle;
    }
    if (!CloseHandle(SharedFileHandles[fHandle].hFile)) {
        return kErrorFromLastError();
    }
    SharedFileHandles[fHandle].hFile = INVALID_HANDLE_VALUE;

    free(SharedFileHandles[fHandle].FileName);
    SharedFileHandles[fHandle].FileName = NULL;

    return 0;
}


/*
    The FindManager tracks in-progress FindFirst/FindNext enumerations.
    The TransactionID is used to identify a specific FindFirst/FindNext
    enumeration, and its associated Win32 FindHandle.

    There are a maximum of 40 in-progress enumerations, matching the
    size of vcefsd\find.c's gFCList[] which is used to track
    enumerations inside WinCE.

*/
class FindManager {
public:
    void PowerOn(void);
    void Reset(void);

    bool IsValidTransactionID(unsigned __int32 FindTransactionID);
    HANDLE GetFindHandle(unsigned __int32 FindTransactionID);
    void SetFindHandle(unsigned __int32 FindTransactionID, HANDLE hFindFile);

private:
    #define    kMaxFC        40 // from vcefsd\find.c

    struct {
        HANDLE hFindFile;
    } FindContext[kMaxFC];
};

FindManager FindManager;

void FindManager::PowerOn(void)
{
    for (int i=0; i<ARRAY_SIZE(FindContext); ++i) {
        FindContext[i].hFindFile = INVALID_HANDLE_VALUE;
    }
}

void FindManager::Reset(void)
{
    for (int i=0; i<ARRAY_SIZE(FindContext); ++i) {
        if (FindContext[i].hFindFile != INVALID_HANDLE_VALUE) {
            FindClose(FindContext[i].hFindFile);
            FindContext[i].hFindFile = INVALID_HANDLE_VALUE;
        }
    }
}

// Determines if a TransactionID corresponds to an in-progress enumeration
//
// Returns:
//  true if it is
//  false if it is not
bool FindManager::IsValidTransactionID(unsigned __int32 FindTransactionID)
{
    if (FindTransactionID >= ARRAY_SIZE(FindContext)) {
        return false;
    }
    return true;
}

// Retrieves a Win32 FindHandle corresponding to the TransactionID
//
// Returns:
//  INVALID_HANDLE_VALUE if the TransacionID is not valid
//  Win32 FindHandle if it is valid
HANDLE FindManager::GetFindHandle(unsigned __int32 FindTransactionID)
{
    if (FindTransactionID >= ARRAY_SIZE(FindContext)) {
        return INVALID_HANDLE_VALUE;
    }
    return FindContext[FindTransactionID].hFindFile;
}

// Associates a TransactionID and FindHandle together.  If the
// TransactionID was already actively enumerating, that old
// enumeration is closed and replaced by the new one.
void FindManager::SetFindHandle(unsigned __int32 FindTransactionID, HANDLE hFindFile)
{
    ASSERT(IsValidTransactionID(FindTransactionID));
    if (FindContext[FindTransactionID].hFindFile != INVALID_HANDLE_VALUE) {
        FindClose(FindContext[FindTransactionID].hFindFile);
    }
    FindContext[FindTransactionID].hFindFile = hFindFile;
}



// Calls GetLastError() and maps the Win32 error to a VCEFSD kError... value.
//
// note: this is a copy of Win32FolderSharingDevice::MapLastErrorToFileError() from Connectix
unsigned __int16 kErrorFromLastError(void)
{
    switch (GetLastError()) {
    case ERROR_HANDLE_DISK_FULL:
        return kErrorDiskFull;
    case ERROR_PATH_NOT_FOUND:
        return kErrorPathNotFound;
    case ERROR_FILE_NOT_FOUND:
        return kErrorFileNotFound;
    case ERROR_WRITE_PROTECT:
        return kErrorWriteProtect;
    case ERROR_FILE_EXISTS:
        return kErrorFileExists;
    case ERROR_TOO_MANY_OPEN_FILES:
        return kErrorTooManyOpenFiles;
    case ERROR_NO_MORE_FILES:
        return kErrorNoMoreFiles;
    case ERROR_INVALID_NAME:
        return kErrorInvalidName;
    case ERROR_ACCESS_DENIED:
        return kErrorAccessDenied;
    case ERROR_LOCK_VIOLATION:
        return kErrorLockViolation;
    case ERROR_SHARING_BUFFER_EXCEEDED:
        return kErrorSharingBufferExceeded;
    case ERROR_GEN_FAILURE:
        return kErrorGeneralFailure;
    case ERROR_SHARING_VIOLATION:
        return kErrorSharingViolation;
    default:
        LOG_ERROR(FOLDERSHARING,"kErrorFromLastError() - unknown Win32 error %d - returning GeneralFailure", GetLastError());
        return kErrorGeneralFailure;
    }
}

IOFolderSharing::IOFolderSharing()
:fDontTriggerInterrupt(false),fGuestReset(false)
{}

bool __fastcall IOFolderSharing::PowerOn(void)
{
    InitializeCriticalSection(&FolderShareLock);
    fNeedsReconfiguration = false;
    NewRoot = NULL;
    hIOComplete = INVALID_HANDLE_VALUE;     // Assume folder sharing is not enabled

    Callback.lpRoutine = CompletionRoutineStatic;
    Callback.lpParameter = this;

    SharedFileHandleManager.PowerOn();
    FindManager.PowerOn();

    if ( Configuration.getFolderShareName() != NULL &&
         !Configuration.NoSecurityPrompt && 
         !PromptToAllow( ID_MESSAGE_ENABLE_FOLDERSHARING, Configuration.getFolderShareName() ) )
    {
        // Disable foldersharing in the configuration object by clearing the path
        Configuration.setFolderShareName(NULL);
    }

    return Reconfigure(Configuration.getFolderShareName());
}

bool __fastcall IOFolderSharing::Reset(void)
{
    if (IsFolderSharingEnabled())
    {
        // Tell the worker thread to reconfigure us at the next safe opportunity
        EnterCriticalSection(&FolderShareLock);
        fNeedsReconfiguration = true;
        fGuestReset = true;
        LeaveCriticalSection(&FolderShareLock);
        CompletionPort.QueueWorkitem(&Callback);
    }
    return true;
}

bool __fastcall IOFolderSharing::Reconfigure(__in_z const wchar_t * NewParam)
{
    EnterCriticalSection(&IOLock);
    EnterCriticalSection(&FolderShareLock);

    if (NewRoot) {
        free(NewRoot);
        NewRoot = NULL;
    }

    DWORD ErrorMessage = ID_MESSAGE_INTERNAL_ERROR;

    if (NewParam) {
        DWORD dwAttributes;

        // Allocate NewRoot on the heap as it must remain intact until the worker thread
        // has a chance to process it
        NewRoot = (wchar_t *)malloc(MAX_PATH*sizeof(wchar_t));
        if (!NewRoot) {
            ErrorMessage = ID_MESSAGE_RESOURCE_EXHAUSTED;
            goto Error;
        }

        // Convert NewParam to a fully-qualifed path, in case the user specified a
        // relative path.
        if (!_wfullpath(NewRoot, NewParam, MAX_PATH)) {
            // Either the path is too long, or the drive letter wasn't valid, etc.
            ErrorMessage = ID_FOLDER_SHARE_PATH_INVALID;
            goto Error;
        }

        size_t cchNewRoot = wcslen(NewRoot);
        if (cchNewRoot == 0) {
            ErrorMessage = ID_FOLDER_SHARE_PATH_EMPTY;
            goto Error;
        }
        if (NewRoot[cchNewRoot-1] == L'\\' || NewRoot[cchNewRoot-1] == '/') {
            // Trim off the trailing '\\' if present
            cchNewRoot--;
            NewRoot[cchNewRoot] = L'\0';
        }

        // Do an existence check on the root directory name
        dwAttributes = GetFileAttributes(NewRoot);
        if (dwAttributes == INVALID_FILE_ATTRIBUTES || (dwAttributes&FILE_ATTRIBUTE_DIRECTORY) == 0) {
            // Directory not found, or the name is a filename instead of a directory name
            ErrorMessage = ID_FOLDER_SHARE_PATH_NOT_FOUND;
            goto Error;
        }

        // From here on, no more validation needs to be done on the folder sharing root.

        // Tell the worker thread to reconfigure us at the next safe opportunity
        fNeedsReconfiguration = true;
        IOPending++;
        LeaveCriticalSection(&FolderShareLock); // avoid lock contention by exiting the critsec before the callback tries to acquire
        CompletionPort.QueueWorkitem(&Callback);

        LOG_INFO(FOLDERSHARING, "Queued request to enable sharing");

    } else {

        // Disable folder sharing if it is enabled
        if (IsFolderSharingEnabled()) {

            // Tell the worker thread to reconfigure us at the next safe opportunity
            if (NewRoot) {
                free(NewRoot);
                NewRoot = NULL;
            }
            fNeedsReconfiguration = true;
            IOPending++;
            LeaveCriticalSection(&FolderShareLock); // avoid lock contention by exiting the critsec before the callback tries to acquire
            CompletionPort.QueueWorkitem(&Callback);
            LOG_INFO(FOLDERSHARING, "Queued request to disable sharing");
        }
        else {
            fDontTriggerInterrupt = false;
            LeaveCriticalSection(&FolderShareLock);
        }
    }

    LeaveCriticalSection(&IOLock);
    return true;

Error:
    // Capture the NewRoot into a local so that we can release the FolderShareLock before displaying UI
    wchar_t * LocalNewRoot = NewRoot;
    NewRoot = NULL;

    // If we suffered an error on the first reconfiguration after a restore clear the flag
    fDontTriggerInterrupt = false;

    LeaveCriticalSection(&FolderShareLock);
    LeaveCriticalSection(&IOLock);

    ShowDialog(ErrorMessage, LocalNewRoot);
    if (LocalNewRoot) {
        free(LocalNewRoot);
    }

    return false;
}

// Async part of reconfiguration.  By running this part of reconfig on the worker thread,
// it guarantees that no other IOFolderSharing::Async...() method is running and so
// reconfiguration will be thread-safe and secure.
//
// Returns:
//  true if folder sharing has been disabled and the worker thread should immediately
//       go idle
//  false if the worker thread can go ahead and execute an async request if there
//       is one queued
void __fastcall IOFolderSharing::AsyncReconfigure(void)
{
    EnterCriticalSection(&IOLock);
    EnterCriticalSection(&FolderShareLock);

    bool fFolderSharingDisabled;
    bool fRaiseInterrupt = false;

    // Reset this to false.  Our caller also set this to false, but then released the FolderShareLock
    // that guards it.  If another reconfigure event happened between then and now, fNeedsConfiguration
    // will be true, but we can handle that reconfiguration now, since it overwrote the older
    // reconfiguration data.
    fNeedsReconfiguration = false;

    // Reset the two managers:  they cache information about the FolderShareRoot
    SharedFileHandleManager.Reset();
    FindManager.Reset();

    if (NewRoot) {
        // Reconfiguration is enabling folder sharing at the directory NewRoot

        fFolderSharingDisabled = false;

        if (FAILED(StringCchCopyW(FolderShareRoot, ARRAY_SIZE(FolderShareRoot), NewRoot))) {
            ASSERT(FALSE); // This should never fail: it is copying from MAX_PATH to MAX_PATH
            fFolderSharingDisabled = true;
            goto Done;
        }
        free(NewRoot);
        NewRoot = NULL;

        // This event (hIOComplete) marks the beginning of actual folder sharing
        if (hIOComplete == INVALID_HANDLE_VALUE) {
            hIOComplete = CreateEvent(NULL, FALSE, FALSE, NULL); // Auto-reset, initially unsignalled
            if (hIOComplete == NULL) {
                fFolderSharingDisabled = true;
                goto Done;
            }
            // Trigger an interrupt on cold boot but on restore
            if (!fDontTriggerInterrupt)
                fRaiseInterrupt = true;
            else
                fDontTriggerInterrupt = false;
        } else {
            // This is a reconfiguration of folder sharing, not an enabling of it, so don't
            // raise the enable/disable change interrupt unless there is a reset.
            if (fGuestReset)
            {
                fRaiseInterrupt = true;
                fGuestReset = false;
            }
        }
        
        LOG_INFO(FOLDERSHARING, "Enabled for directory %S", FolderShareRoot);
    } else {
        // Reconfiguration is disabling folder sharing

        fFolderSharingDisabled = true;
        fRaiseInterrupt = !fDontTriggerInterrupt;
        fDontTriggerInterrupt = false;

        if (!fGuestReset)
        {
            // Complete the async IO at the top of the queue by reporting "General Failure"
            SignalIOComplete(kErrorGeneralFailure);

            // disable folder sharing
            CloseHandle(hIOComplete);
            hIOComplete = INVALID_HANDLE_VALUE;

            LOG_INFO(FOLDERSHARING, "Disabled");
        }
        fGuestReset = false;
    }

    if (fRaiseInterrupt) {
        // Notify the guest OS that the folder sharing state has changed.  This must
        // also be under the IOLock to avoid reentrancy in the interrupt contoller.
        EmulServ.RaiseInterrupt(EmulServ_FolderSharingChanged);
    }

Done:
    if (fFolderSharingDisabled) {
        LeaveCriticalSection(&FolderShareLock);
        LeaveCriticalSection(&IOLock);
        SignalIOComplete(Result);
    } else {
        LeaveCriticalSection(&FolderShareLock);
        IOPending = IOPending ? IOPending - 1 : 0;
        LeaveCriticalSection(&IOLock);
    }
}


void __fastcall IOFolderSharing::SaveState(StateFiler& filer) const
{
    filer.Write('FLDS');
    filer.Write(ServerPBAddr);
    filer.Write(Code);
    //filer.Write(IOPending); // This is not saved to disk
    filer.Write(Result);
}

void __fastcall IOFolderSharing::RestoreState(StateFiler& filer)
{
    filer.Verify('FLDS');
    filer.Read(ServerPBAddr);
    filer.Read(Code);
    // filer.Read(IOPending); // This is not saved to disk
    filer.Read(Result);
    // we need to trigger an interrupt on power on - only if we restoring from global state
    fDontTriggerInterrupt = Configuration.FolderSettingsChanged ? false : true;
}

unsigned __int32 __fastcall IOFolderSharing::ReadWord(unsigned __int32 IOAddress)
{
    switch (IOAddress) {
    case 0:
        return ServerPBAddr;
    case 4:
        return Code;
    case 8:
        return (IOPending > 0 ? 1 : 0);
    case 12:
        return Result;
    default:
        TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
    }
}

void __fastcall IOFolderSharing::WriteWord(unsigned __int32 IOAddress, unsigned __int32 Value)
{
    switch (IOAddress) {
    case 0:
        ServerPBAddr = Value;
        break;
    case 4:
        Code = Value;
        if (!IsFolderSharingEnabled()) {
            IOPending=0;        // Indicate no I/O is pending
            Result=kErrorInvalidFunction;  // Indicate that the operation failed
            break;
        }

        if (IsFolderSharingLoggingEnabled(VERBOSE)) {
            static unsigned __int32 PreviousValue;

            // Log repeated calls to kServerPollCompletion only one time
            if (Value == kServerPollCompletion && Value != PreviousValue) {
                LOG_VERBOSE(FOLDERSHARING, "ServerPollCompletion()");
            }
            PreviousValue = Value;
        }
        if (Code != kServerPollCompletion) {
            if (IOPending) {
                // An async I/O (or async reconfigure) is already in progress
                Result=kErrorInvalidFunction;
                break;
            }
            IOPending=1; // indicate that async IO is about to take place
        }
        switch (Code) {
        case kServerPollCompletion:
            ServerPollCompletion();
            break;
        case kServerGetDriveConfig:
            ServerGetDriveConfig();
            break;
        case kServerCreate:
            AsyncCall(AsyncServerCreate);
            break;
        case kServerOpen:
            AsyncCall(AsyncServerOpen);
            break;
        case kServerRead:
            AsyncCall(AsyncServerRead);
            break;
        case kServerWrite:
            AsyncCall(AsyncServerWrite);
            break;
        case kServerSetEOF:
            AsyncCall(AsyncServerSetEOF);
            break;
        case kServerClose:
            AsyncCall(AsyncServerClose);
            break;
        case kServerGetSpace:
            AsyncCall(AsyncServerGetSpace);
            break;
        case kServerMkDir:
            AsyncCall(AsyncServerMkDir);
            break;
        case kServerRmDir:
            AsyncCall(AsyncServerRmDir);
            break;
        case kServerSetAttributes:
            AsyncCall(AsyncServerSetAttributes);
            break;
        case kServerRename:
            AsyncCall(AsyncServerRename);
            break;
        case kServerDelete:
            AsyncCall(AsyncServerDelete);
            break;
        case kServerGetInfo:
            AsyncCall(AsyncServerGetInfo);
            break;
        case kServerGetFCBInfo:
            AsyncCall(AsyncServerGetFCBInfo);
            break;
        case kServerGetMaxIOSize:
            ServerGetMaxIOSize();
            break;
        default:
            TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE); 
            break;
        }
        break;
    case 8:
        // Ignore writes to IOPending.  
        break;
    case 12:
        Result = Value;
        break;
    default:
        TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE); 
        break;
    }
}

// Get a Win32 pointer to the WinCE-side ServerPB structure.
//
// Returns:
//  Pointer to the ServerPB structure.  If the WinCE address doens't
//  point into guest RAM, then the emulator exits via TerminateWithMessage()
IOFolderSharing::PServerPB IOFolderSharing::GetServerPB(void)
{
    size_t HostEffectiveAddress;
    PServerPB pServerPB;

    // Although vcefsd\main.cpp indicates that gpServerPB is a physical address, it really
    // isn't.  It is a kernel-mode address 0x0c200000.  That value|0x80000000 is what
    // is passed to VirtualCopy() as the 
    HostEffectiveAddress = BoardMapGuestPhysicalToHostRAM(ServerPBAddr);
    if (!HostEffectiveAddress) {
        TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
    }

    pServerPB = (IOFolderSharing::PServerPB)HostEffectiveAddress;

    // Confirm that the structure size/version is the expected value
    if (pServerPB->fStructureSize != sizeof(*pServerPB)) {
        TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
    }

    return pServerPB;
}

// Poll to determine if an async I/O operation has completed.  Updates
// the value of the "Result" register with the results.
//
// The guest-side of folder sharing calls PollCompletion() in a loop
// waiting for the Result field to become nonzero, indicating that
// the async folder share call has completed.  This allows guest
// interrupts to continue to fire during long-running I/O calls.
//
// Avoid having the emulator use 100% CPU by blocking until one
// of three conditions is true:
// 1.  Folder sharing I/O completes
// 2.  CPU interrupt is ready for delivery
// 3.  1ms has passed
void __fastcall IOFolderSharing::ServerPollCompletion(void)
{
    DWORD dw;
    HANDLE Handles[2];

    if (!IOPending) { // No IO is currently pending - return without the overhead of calling WFMO
        LOG_VERBOSE(FOLDERSHARING, "ServerPollCompletion() - IOPending was zero.  Operation completed quickly.");
        Result = kErrorNoError;
        return;
    }
    Handles[0] = hIOComplete;       // folder sharing I/O completion
    Handles[1] = hIdleEvent;        // CPU interrupt pending
    LeaveCriticalSection(&IOLock);  // leave the I/O lock so interrupts can be delivered, etc.
    dw = WaitForMultipleObjects(2, Handles, FALSE, 1);
    EnterCriticalSection(&IOLock);
    if (dw == WAIT_OBJECT_0) {
        // Folder sharing IO completed OK
        LOG_VERBOSE(FOLDERSHARING, "ServerPollCompletion() - hIOComplete was signalled.  Operation has completed.");
    } else {
        ASSERT(dw == WAIT_OBJECT_0+1 || dw == WAIT_TIMEOUT);
        Result = kErrorNoError;
    }
}

// ServerGetDriveConfig - determines if folder sharing is enabled or not
// 
// in: ServerPB->fVRefNum = 0
// out: ServerPB->fResult == 0 for success, nonzero for failure
void __fastcall IOFolderSharing::ServerGetDriveConfig(void)
{
    if (IsFolderSharingEnabled()) {
        Result = kErrorNoError;
    } else {
        Result = kErrorGeneralFailure;
    }
    LOG_VERBOSE(FOLDERSHARING, "ServerGetDriveConfig() - folder sharing is%s enabled", (Result) ? " not" : "");
    SignalIOComplete(Result);
}

// Map a VCEFSD fOpenMode to a Win32 dwDesiredAccess value
//
// Returns:
//  true if the map is successful
//  false if fOpenMode doesn't correspond to a valid Win32 dwDesiredAccess
bool IOFolderSharing::DesiredAccessFromOpenMode(UInt16 OpenMode, DWORD *pdwDesiredAccess)
{
    OpenMode &= 0xf;

    switch (OpenMode) {
    case kOpenAccessReadOnly:
        *pdwDesiredAccess = GENERIC_READ;
        break;

    case kOpenAccessWriteOnly:
        *pdwDesiredAccess = GENERIC_WRITE;
        break;

    case kOpenAccessReadWrite:
        *pdwDesiredAccess = GENERIC_READ|GENERIC_WRITE;
        break;

    default:
        *pdwDesiredAccess = 0;
        return false;
    }

    return true;
}

// Map a VCEFSD fOpenMode to a Win32 dwShareMode
//
// Returns:
//  true if the mapping is successful
//  false if the fOpenMode does not map to a valid Win32 dwShareMode
bool IOFolderSharing::ShareModeFromOpenMode(UInt16 OpenMode, DWORD *pdwShareMode)
{
    OpenMode &= 0xf0;

    switch (OpenMode) {
    case kOpenShareDenyReadWrite:
        *pdwShareMode = 0;
        break;

    case kOpenShareDenyWrite:
        *pdwShareMode = FILE_SHARE_READ;
        break;

    case kOpenShareDenyRead:
        *pdwShareMode = FILE_SHARE_WRITE;
        break;

    case kOpenShareDenyNone:
        *pdwShareMode = FILE_SHARE_READ|FILE_SHARE_WRITE;
        break;

    case kOpenShareCompatibility: // error:  this shouldn't be passed from the driver
    default:
        *pdwShareMode = 0;
        return false;
    }

    return true;
}

// ServerCreate - create a new empty file.  The filename must not already exist.
// 
//Parameters:
//    In:     inPB            - Ptr to a Server Parameter block
//            ->u.fXXX.fName  - the file name (fully qualified)
//    Out:    inPB            - Info returned in the parameter block
//            ->fResult       - Error code
unsigned __int32 __fastcall IOFolderSharing::AsyncServerCreate(class IOFolderSharing *pThis, PServerPB pServerPB)
{
    if (IsFolderSharingLoggingEnabled(VERBOSE)) {
        wchar_t fName[MAX_PATH];
        StringCchCopyNW(fName, ARRAY_SIZE(fName), pServerPB->u.fLfn.fName, pServerPB->u.fLfn.fNameLength/sizeof(wchar_t));

        LOG_VERBOSE(FOLDERSHARING, "AsyncServerCreate() - fName='%S'", fName);
    }

    // Capture the path name to create
    wchar_t FileName[MAX_PATH];
    UInt16 kError;
    kError = pThis->GetWin32Path(pServerPB->u.fLfn.fName, pServerPB->u.fLfn.fNameLength,
                                FileName, ARRAY_SIZE(FileName));
    if (kError != kErrorNoError) {
        return kError;
    }

    // Create the file
    HANDLE hFile;
    hFile = CreateFileW(FileName, 
                        GENERIC_WRITE, 
                        FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE, 
                        NULL, 
                        CREATE_NEW, 
                        0, 
                        NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        return kErrorFromLastError();
    }
    // else success
    CloseHandle(hFile);

    LOG_VERBOSE(FOLDERSHARING, "ServerCreate() - success");
    return kErrorNoError; // indicate success
}

// ServerOpen - open an existing file and return a handle to it
//
//Parameters:
//    In:     inPB            - Ptr to a Server Parameter block
//            ->u.fXXX.fName  - the file name (fully qualified)
//            ->fOpenMode     - Open mode attributes (read only...)
//    Out:    inPB            - Info returned in the parameter block
//            ->fResult       - Error code
//            ->fHandle       - Handle to the file
unsigned __int32 __fastcall IOFolderSharing::AsyncServerOpen(class IOFolderSharing *pThis, PServerPB pServerPB)
{
    wchar_t FileName[MAX_PATH];
    DWORD dwDesiredAccess;
    DWORD dwShareMode;
    HANDLE hFile;
    UInt16 fHandle;
    UInt16 OpenMode;
    wchar_t *FileNameCopy = NULL;

    if (IsFolderSharingLoggingEnabled(VERBOSE)) {
        wchar_t fName[MAX_PATH]; fName[0] = 0;
        StringCchCopyNW(fName, ARRAY_SIZE(fName), pServerPB->u.fLfn.fName, pServerPB->u.fLfn.fNameLength/sizeof(wchar_t));
        LOG_VERBOSE(FOLDERSHARING, "AsyncServerOpen() - fName='%S', fOpenMode=0x%x", fName, pServerPB->fOpenMode);
    }

    // Allocate a shared file handle slot
    UInt16 kError;
    fHandle = SharedFileHandleManager.AllocateSharedFileHandle();
    if (fHandle == InvalidSharedFileHandle) {
        kError = kErrorTooManyOpenFiles;
        goto Failure;
    }

    // Capture the path name to open
    kError = pThis->GetWin32Path(pServerPB->u.fLfn.fName, pServerPB->u.fLfn.fNameLength,
                                FileName, ARRAY_SIZE(FileName));
    if (kError != kErrorNoError) {
        goto Failure;
    }

    // Crack the fOpenMode field
    OpenMode = pServerPB->fOpenMode;
    if (!DesiredAccessFromOpenMode(OpenMode, &dwDesiredAccess)) {
        kError = kErrorInvalidFunction;
        goto Failure;
    }
    if (!ShareModeFromOpenMode(OpenMode, &dwShareMode)) {
        kError = kErrorInvalidFunction;
        goto Failure;
    }

    // Copy the WinCE filename into a heap allocation for handoff to the SharedFileHandleManager
    FileNameCopy = _wcsdup(FileName+wcslen(pThis->FolderShareRoot));
    if (FileNameCopy == NULL) {
        goto Failure;
    }

    // Open the file
    hFile = CreateFileW(FileName, 
                        dwDesiredAccess, 
                        dwShareMode, 
                        NULL, 
                        OPEN_EXISTING, 
                        0, 
                        NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        kError = kErrorFromLastError();
        goto Failure;
    }
    // else success

    // Store the Win32 file HANDLE in the shared file handle table
    SharedFileHandleManager.SetSharedFileHandle(fHandle, hFile, OpenMode, FileNameCopy);
    FileNameCopy = NULL; // the SharedFileManager now owns the heap allocation

    // return the shared file handle value back to the VCEFSD
    pServerPB->fHandle = fHandle;

    kError = kErrorNoError; // indicate success
    LOG_VERBOSE(FOLDERSHARING, "ServerOpen() - success, fHandle=0x%x", pServerPB->fHandle);

Failure:
    if (FileNameCopy) {
        free(FileNameCopy);
    }
    return kError;
}

// ServerSetEOF - extend or truncate a file length
//
//Parameters:
//    In:     inPB            - Ptr to a Server Parameter block
//            ->fHandle       - Handle to the file
//            ->fPosition     - The new EOF mark
//    Out:    inPB            - Info returned in the parameter block
//            ->fResult       - Error code
unsigned __int32 IOFolderSharing::AsyncServerSetEOF(class IOFolderSharing *pThis, PServerPB pServerPB)
{
    LOG_VERBOSE(FOLDERSHARING, "ServerSetEOF() - fHandle=0x%x, fPosition=0x%x", pServerPB->fHandle, pServerPB->fPosition);

    HANDLE hFile = SharedFileHandleManager.GetSharedFileHandle(pServerPB->fHandle);
    if (hFile == INVALID_HANDLE_VALUE) {
        return kErrorInvalidHandle;
    }

    LARGE_INTEGER Pos;
    Pos.QuadPart = pServerPB->fPosition;
    if (!SetFilePointerEx(hFile, Pos, NULL, FILE_BEGIN)) {
        return kErrorFromLastError();
    }

    if (!SetEndOfFile(hFile)) {
        return kErrorFromLastError();
    }

    LOG_VERBOSE(FOLDERSHARING, "ServerSetEOF() - success");
    return kErrorNoError;
}


// Map a Win32 FILETIME to a VCEFSD date/time value.  Note that
// FILETIMEs represent a larger range of dates and times
//
// Return:
//  0 - conversion failed, likely due to time range problem
//  nonzero - VCEFSD date/time value
unsigned __int32 IOFolderSharing::FiletimeToLong(const FILETIME *ft)
{
    WORD        dosDate;
    WORD        dosTime;
    FILETIME    LocalFileTime;

    // Convert from Coordinated Universal Time (UTC) to a local file time. 
    if (::FileTimeToLocalFileTime(ft, &LocalFileTime)) {
        // Note : MS-DOS date format can represent only dates 
        // between 1/1/1980 and 12/31/2107
        if (::FileTimeToDosDateTime (&LocalFileTime, &dosDate, &dosTime)) {
            return MAKELONG(dosTime, dosDate);
        }
    }
    return 0;
}

// Map a VCEFSD date/time value to a Win32 FILETIME
//
// Return:
//  true on success
//  false on conversion failure
bool IOFolderSharing::LongToFiletime(unsigned __int32 fFileTimeDate, FILETIME *ft)
{
    FILETIME LocalFileTime;
    WORD dosDate = HIWORD(fFileTimeDate);
    WORD dosTime = LOWORD(fFileTimeDate);

    if (!DosDateTimeToFileTime(dosDate, dosTime, &LocalFileTime)) {
        return false;
    }
    if (!LocalFileTimeToFileTime(&LocalFileTime, ft)) {
        return false;
    }
    return true;
}


// Given a WinCE path from the ServerPB, returns back a fully-qualified Win32 path
// guaranteed to be a child of the FolderShareRoot path.
//
// Note that cbWinCEPath is a count of BYTES in WinCEPath, not including a NULL
// terminator.
//
// Return:
//  a kError value
IOFolderSharing::UInt16 IOFolderSharing::GetWin32Path(__out_bcount(cbWinCEPath) wchar_t *WinCEPath, size_t cbWinCEPath, __inout_ecount(cchWin32Path) wchar_t *Win32Path, size_t cchWin32Path)
{
    HRESULT hr;
    wchar_t TempPath[MAX_PATH];
    size_t cchFolderShareRoot;
 
    if (cchWin32Path != MAX_PATH) {
        ASSERT(FALSE);
        return kErrorGeneralFailure;
    }

    Win32Path[0] = 0;

    hr = StringCchCopyW(TempPath, ARRAY_SIZE(TempPath), FolderShareRoot);
    if (FAILED(hr)) {
        return kErrorInvalidName;
    }

    hr = StringCchCatN(TempPath, ARRAY_SIZE(TempPath), WinCEPath, cbWinCEPath/sizeof(wchar_t));
    if (FAILED(hr)) {
        return kErrorInvalidName;
    }
    if (PathCanonicalizeW(Win32Path, TempPath) == FALSE) {
        return kErrorInvalidName;
    }
    cchFolderShareRoot = wcslen(FolderShareRoot);
    if (_wcsnicmp(Win32Path, FolderShareRoot, cchFolderShareRoot)){
        return kErrorInvalidName; // The full path isn't prefixed by FolderShareRoot any more!
    }

    if (Win32Path[cchFolderShareRoot] != L'\\' && Win32Path[cchFolderShareRoot] != L'/') {
        // The WinCE name must be fully-qualifed (beginning with a '\\' character)
        return kErrorInvalidName;
    }

    return kErrorNoError;
}

// Convert a Win32 dwFileAttributes to its WinCE equivalent
IOFolderSharing::UInt16 IOFolderSharing::GetCEFileAttributes(DWORD dwWin32FileAttributes)
{
    return (UInt16)(dwWin32FileAttributes & 
                        (FILE_ATTRIBUTE_READONLY |
                            FILE_ATTRIBUTE_SYSTEM |
                            FILE_ATTRIBUTE_HIDDEN |
                            FILE_ATTRIBUTE_ARCHIVE |
                            FILE_ATTRIBUTE_DIRECTORY));
}

// Write back the "out" parameters resulting from a ServerGetInfo() call
void IOFolderSharing::UpdateResultsFromFind(PServerPB pServerPB, LPWIN32_FIND_DATA lpFindData, bool fWritebackName)
{
    pServerPB->fFileCreateTimeDate = FiletimeToLong(&lpFindData->ftCreationTime);
    pServerPB->fFileTimeDate = FiletimeToLong(&lpFindData->ftLastWriteTime);
    pServerPB->fFileAttributes  = GetCEFileAttributes(lpFindData->dwFileAttributes);

    if (lpFindData->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
        pServerPB->fSize = 0;
    } else {
        pServerPB->fSize = (lpFindData->nFileSizeHigh) ? 0xffffffff : lpFindData->nFileSizeLow;
    }

    if (fWritebackName) {
        pServerPB->u.fLfn.fNameLength = (UInt16)(wcslen(lpFindData->cFileName)*sizeof(wchar_t));
        if (FAILED(StringCchCopyW(pServerPB->u.fLfn.fName, ARRAY_SIZE(pServerPB->u.fLfn.fName), 
                                lpFindData->cFileName))) {
            ASSERT(FALSE); // copying from a MAX_PATH array to another MAX_PATH array shouldn't fail
        }
    }
}


// ServerGetInfo() - FindFirst/FindNext and file/directory existence check
//
//    In:     inPB            - Ptr to a Server Parameter block
//            ->fTransactionID - WinCE-allocated TransactionID, or 0xffffffff for no transaction
//            ->fIndex
//                      positive: get info about file or directory specified by fIndex (FindNext)
//                      zero    : get info for the file or directory specified by the fName (FindFirst)
//                      negative: get info for the file or directory specified (existence check)
//            ->fName         - optional filename, depending on fIndex value.  May contain a wildcard.
//
//    Out:    inPB            - Info returned in the parameter block
//            ->fResult       - Error code
//            ->fFileDateTime
//            ->fSize (0 for directories)
//            ->fFileAttributes
//            ->fName if the fTransactionID is not 0xffffffff
unsigned __int32 __fastcall IOFolderSharing::AsyncServerGetInfo(IOFolderSharing *pThis, PServerPB pServerPB)
{
    UInt16 kError;
    if (IsFolderSharingLoggingEnabled(VERBOSE)) {
        wchar_t fName[MAX_PATH]; fName[0] = 0;
        StringCchCopyNW(fName, ARRAY_SIZE(fName), pServerPB->u.fLfn.fName, pServerPB->u.fLfn.fNameLength/sizeof(wchar_t));
        LOG_VERBOSE(FOLDERSHARING, "ServerGetInfo() - fName='%S', fIndex=%d, fTransactionID=0x%x", 
               fName, pServerPB->fIndex, pServerPB->fFindTransactionID);
    }

    SInt16 fIndex = pServerPB->fIndex;
    if (fIndex == -1) {
        // pServerPB->u.fLfn.fNameLength and pServerPB->u.fLfn.fName are the filename to look up
        //
        // This is a one-shot lookup of a single file/directory name to look up, not part of a
        // FindFirst/FindNext loop.  fFindTransactionID is always 0xffffffff.
        //
        //
        wchar_t FileName[_MAX_PATH];
        WIN32_FILE_ATTRIBUTE_DATA FileAttributes;

        // find.c increments fIndex to 0 if fWildCard is true, so if fIndex is -1, there
        // is no wildcard present in the path.
        ASSERT(pServerPB->fWildCard == FALSE);  

        if (pServerPB->fFindTransactionID != 0xffffffff) {
            return kErrorInvalidFunction;
        }

        // Capture the path name to search for
        kError = pThis->GetWin32Path(pServerPB->u.fLfn.fName, pServerPB->u.fLfn.fNameLength,
                                 FileName, ARRAY_SIZE(FileName));
        if (kError != kErrorNoError) {
            return kError;
        }

        if (GetFileAttributesEx(FileName, GetFileExInfoStandard, &FileAttributes) == 0) {
            return kErrorFromLastError();
        }

        pServerPB->fFileCreateTimeDate = FiletimeToLong(&FileAttributes.ftCreationTime);
        pServerPB->fFileTimeDate = FiletimeToLong(&FileAttributes.ftLastWriteTime);
        pServerPB->fFileAttributes  = GetCEFileAttributes(FileAttributes.dwFileAttributes);

        if (FileAttributes.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            pServerPB->fSize = 0;
        } else {
            pServerPB->fSize = (FileAttributes.nFileSizeHigh) ? 0xffffffff : FileAttributes.nFileSizeLow;
        }
    } else if (fIndex == 0) {
        // Beginning a FindFirst/FindNext loop with a wildcard pattern in pServerPB->u.fLfn.fName
        wchar_t FileName[_MAX_PATH];
        HANDLE hFindFile;
        WIN32_FIND_DATA FindFileData;
        unsigned __int32 fFindTransactionID;

        // Capture and validate the transaction ID
        fFindTransactionID = pServerPB->fFindTransactionID;
        if (fFindTransactionID != 0xffffffff && !FindManager.IsValidTransactionID(fFindTransactionID)) {
            return kErrorGeneralFailure;
        }

        // Capture the path name to search for
        kError = pThis->GetWin32Path(pServerPB->u.fLfn.fName, pServerPB->u.fLfn.fNameLength,
                                     FileName, ARRAY_SIZE(FileName));
        if (kError != kErrorNoError) {
            return kError;
        }

        // Begin the enumeration
        hFindFile = FindFirstFile(FileName, &FindFileData);
        if (hFindFile == INVALID_HANDLE_VALUE) {
            return kErrorFromLastError();
        }

        // If this is really a WinCE FindFirst/FindNext enumeration, and the returned 
        // name is "." or "..", ignore it and find the next entry.
        while (wcscmp(FindFileData.cFileName, L".") == 0 || wcscmp(FindFileData.cFileName, L"..") == 0) {
            if (!FindNextFile(hFindFile, &FindFileData)) {
                // Close the hFindFile, as the enumeration has ended.
                kError = kErrorFromLastError();
                FindClose(hFindFile);
                return kError;
            }
        }

        if (fFindTransactionID == 0xffffffff) {
            FindClose(hFindFile);
        } else {
            // Preserve the enumeration handle for the FindNext call
            FindManager.SetFindHandle(fFindTransactionID, hFindFile);
        }

        // Fill in the results of the enumeration
        pThis->UpdateResultsFromFind(pServerPB, &FindFileData, (fFindTransactionID != 0xffffffff));

    } else {
        HANDLE hFindFile;
        WIN32_FIND_DATA FindFileData;
        unsigned __int32 fFindTransactionID;
        
        // Get the enumeration handle
        fFindTransactionID = pServerPB->fFindTransactionID;
        hFindFile = FindManager.GetFindHandle(fFindTransactionID);
        if (hFindFile == INVALID_HANDLE_VALUE) {
            return kErrorGeneralFailure;
        }

        // Find the next file, ignoring "." and ".."
        do {
            if (!FindNextFile(hFindFile, &FindFileData)) {
                // Close the hFindFile, as the enumeration has ended.
                kError = kErrorFromLastError();
                FindManager.SetFindHandle(fFindTransactionID, INVALID_HANDLE_VALUE);
                return kError;
            }
        } while (wcscmp(FindFileData.cFileName, L".") == 0 || wcscmp(FindFileData.cFileName, L"..") == 0);

        pThis->UpdateResultsFromFind(pServerPB, &FindFileData, true);
    }

    LOG_VERBOSE(FOLDERSHARING, "ServerGetInfo() - success: fFileCreateTime=0x%x, fSize=0x%x, fFileAttributes=0x%x",
        pServerPB->fFileCreateTimeDate,
        pServerPB->fSize,
        pServerPB->fFileAttributes);

    return kErrorNoError; // indicate success
}

// ServerClose - close a fHandle
//
//Parameters:
//    In:     inPB            - Ptr to a Server Parameter block
//            ->fHandle       - Handle to the file
//    Out:    inPB            - Info returned in the parameter block
//            ->fResult       - Error code
unsigned __int32 __fastcall IOFolderSharing::AsyncServerClose(class IOFolderSharing *pThis, PServerPB pServerPB)
{
    LOG_VERBOSE(FOLDERSHARING, "ServerClose() - fHandle=0x%x", pServerPB->fHandle);

    // This needs to be done async in order to prevent a close from happening while an async
    // call is in progress.  The risk is that the Win32 HANDLE might be re-used after this
    // CloseHandle() and accidentally allow an AsyncServerRead() to read from a HANDLE not
    // shared with the guest OS.
    
    return SharedFileHandleManager.CloseSharedFileHandle(pServerPB->fHandle);
}

// ServerRead - implements ReadFile()
//
//Parameters:
//    In:     inPB            - Ptr to a Server Parameter block
//            ->fHandle       - Handle to the file
//            ->fSize         - # of bytes to read
//            ->fPosition     - starting position to read from
//            ->fDTAPtr       - Disk Transfer Address (PC read buffer)
//    Out:    inPB            - Info returned in the parameter block
//            ->fResult       - Error code
//            ->fSize         - # of bytes actually Read
unsigned __int32 __fastcall IOFolderSharing::AsyncServerRead(IOFolderSharing *pThis, PServerPB pServerPB)
{
    LOG_VERBOSE(FOLDERSHARING, "ServerRead() - fHandle=0x%x, fSize=0x%x, fPosition=0x%x",
        pServerPB->fHandle, pServerPB->fSize, pServerPB->fPosition);

    HANDLE hFile = SharedFileHandleManager.GetSharedFileHandle(pServerPB->fHandle);
    if (hFile == INVALID_HANDLE_VALUE) {
        return kErrorInvalidHandle;
    }

    LPVOID lpvBuffer = (LPVOID)BoardMapGuestPhysicalToHostRAM(PtrToLong(pServerPB->fDTAPtr));
    if (lpvBuffer == NULL) {
        return kErrorReadFault;
    }

    LARGE_INTEGER Pos;
    Pos.QuadPart = pServerPB->fPosition;
    if (!SetFilePointerEx(hFile, Pos, NULL, FILE_BEGIN)) {
        return kErrorFromLastError();
    }

    DWORD dwBytesRead;
    if (!ReadFile(hFile, lpvBuffer, pServerPB->fSize, &dwBytesRead, NULL)) {
        return kErrorFromLastError();
    }

    // Success
    pServerPB->fSize = dwBytesRead;
    LOG_VERBOSE(FOLDERSHARING, "ServerRead() - success: fSize=0x%x",pServerPB->fSize);
    return kErrorNoError;
}


// ServerWrite - implements WriteFile()
//
//Parameters:
//    In:     inPB            - Ptr to a Server Parameter block
//            ->fHandle       - Handle to the file
//            ->fSize         - # of bytes to write
//            ->fPosition     - starting position to write to
//            ->fDTAPtr       - Disk Transfer Address (PC read buffer)
//    Out:    inPB            - Info returned in the parameter block
//            ->fResult       - Error code
//            ->fSize         - # of bytes actually Written
unsigned __int32 __fastcall IOFolderSharing::AsyncServerWrite(IOFolderSharing *pThis, PServerPB pServerPB)
{
    LOG_VERBOSE(FOLDERSHARING, "ServerWrite() - fHandle=0x%x, fSize=0x%x, fPosition=0x%x",
        pServerPB->fHandle, pServerPB->fSize, pServerPB->fPosition);

    HANDLE hFile = SharedFileHandleManager.GetSharedFileHandle(pServerPB->fHandle);
    if (hFile == INVALID_HANDLE_VALUE) {
        return kErrorInvalidHandle;
    }

    LPVOID lpvBuffer = (LPVOID)BoardMapGuestPhysicalToHostRAM(PtrToLong(pServerPB->fDTAPtr));
    if (lpvBuffer == NULL) {
        return kErrorWriteFault;
    }

    LARGE_INTEGER Pos;
    Pos.QuadPart = pServerPB->fPosition;
    if (!SetFilePointerEx(hFile, Pos, NULL, FILE_BEGIN)) {
        return kErrorFromLastError();
    }

    DWORD dwBytesWritten;
    if (!WriteFile(hFile, lpvBuffer, pServerPB->fSize, &dwBytesWritten, NULL)) {
        return kErrorFromLastError();
    }

    // Success
    pServerPB->fSize = dwBytesWritten;
    LOG_VERBOSE(FOLDERSHARING, "ServerWrite() - success: fSize=0x%x",pServerPB->fSize);
    return kErrorNoError;
}

// ServerGetFCBInfo - return metadata for a file specified by fHandle
//
//Parameters:
//    In:     inPB            - Ptr to a Server Parameter block
//            ->fHandle       - Handle to the file
//    Out:    inPB            - Info returned in the parameter block
//            ->fResult       - Error code
//            ->u.fXXX.fName  - the file name
//            ->fPosition     - The current file position
//            ->fSize         - The length of the file
//            ->fOpenMode     - The open mode
//            ->fFileAttributes-0 - R/W, 1 = Read Only
unsigned __int32 __fastcall IOFolderSharing::AsyncServerGetFCBInfo(IOFolderSharing *pThis, PServerPB pServerPB)
{
    LOG_VERBOSE(FOLDERSHARING, "ServerGetFCBInfo() - fHandle=0x%x", pServerPB->fHandle);

    UInt16 fHandle = pServerPB->fHandle;
    HANDLE hFile = SharedFileHandleManager.GetSharedFileHandle(fHandle);
    if (hFile == INVALID_HANDLE_VALUE) {
        return kErrorInvalidHandle;
    }

    LARGE_INTEGER CurrentPos;
    LARGE_INTEGER Zero;

    // Get the current file position
    Zero.QuadPart=0;
    if (!SetFilePointerEx(hFile, Zero, &CurrentPos, FILE_CURRENT)) {
        return kErrorFromLastError();
    }

    // Get the file size and attributes
    BY_HANDLE_FILE_INFORMATION FileInfo;
    if (!GetFileInformationByHandle(hFile, &FileInfo)) {
        return kErrorFromLastError();
    }

    // Get the filename and open mode
    wchar_t *FileName;
    UInt16 OpenMode;
    if (!SharedFileHandleManager.GetSharedFileHandleAttributes(fHandle, &OpenMode, &FileName)) {
        ASSERT(FALSE); // the invalid handle should have been caught by the GetSharedFileHandle() call
        return kErrorInvalidHandle;
    }

    // Success - update pServerPB
    pServerPB->fPosition = (CurrentPos.HighPart) ? 0xffffffff : CurrentPos.LowPart;
    pServerPB->fSize = (FileInfo.nFileSizeHigh) ? 0xffffffff : FileInfo.nFileSizeLow;
    pServerPB->fFileAttributes = (UInt16)GetCEFileAttributes(FileInfo.dwFileAttributes);
    pServerPB->fOpenMode = OpenMode;
    pServerPB->u.fLfn.fNameLength = (UInt16)(wcslen(FileName)*sizeof(wchar_t));
    if (FAILED(StringCchCopyW(pServerPB->u.fLfn.fName, 
               ARRAY_SIZE(pServerPB->u.fLfn.fName), 
               FileName))) {
        return kErrorGeneralFailure;
    }

    LOG_VERBOSE(FOLDERSHARING, "ServerGetFCBInfo() - success: fName='%S', fPosition=0x%x, fSize=0x%x, fOpenMode=0x%x, fFileAatributes=0x%x",
        FileName,
        pServerPB->fPosition,
        pServerPB->fSize,
        pServerPB->fOpenMode,
        pServerPB->fFileAttributes);
        
    return kErrorNoError;
}

// ServerGetSpace - return disk free space information about the shared folder directory
//
//Parameters:
//    In:     inPB            - Ptr to a Server Parameter block
//    Out:    inPB            - Info returned in the parameter block
//            ->fResult       - Error code
//            ->fSize         - Free space on the drive / 32k
//            ->fPosition     - Total space on drive / 32k
unsigned __int32 __fastcall IOFolderSharing::AsyncServerGetSpace(class IOFolderSharing *pThis, PServerPB pServerPB)
{
    LOG_VERBOSE(FOLDERSHARING, "ServerGetSpace()");

    ULARGE_INTEGER FreeBytesAvailable;
    ULARGE_INTEGER TotalNumberOfBytes;
    if (!GetDiskFreeSpaceEx(pThis->FolderShareRoot,
                            &FreeBytesAvailable,
                            &TotalNumberOfBytes,
                            NULL)) {
        return kErrorFromLastError();
    }
    if (FreeBytesAvailable.HighPart) {
        FreeBytesAvailable.LowPart = 0xffffffff;
    }
    if (TotalNumberOfBytes.HighPart) {
        TotalNumberOfBytes.LowPart = 0xffffffff;
    }
    pServerPB->fSize = FreeBytesAvailable.LowPart/32768;
    pServerPB->fPosition = TotalNumberOfBytes.LowPart/32768;

    LOG_VERBOSE(FOLDERSHARING, "ServerGetDiskSpace() - success: fSize=0x%x (FreeSpace=%u bytes), fPosition=0x%x (TotalSpace=%u bytes)",
        pServerPB->fSize, pServerPB->fSize*32768,
        pServerPB->fPosition, pServerPB->fPosition*32768);

    return kErrorNoError;
}

// ServerMakeDir - create a new directory
//
//Parameters:
//    In:     inPB            - Ptr to a Server Parameter block
//            ->u.fXXX.fName  - the file name
//    Out:    inPB            - Info returned in the parameter block
//            ->fResult       - Error code
unsigned __int32 __fastcall IOFolderSharing::AsyncServerMkDir(class IOFolderSharing *pThis, PServerPB pServerPB)
{
    if (IsFolderSharingLoggingEnabled(VERBOSE)) {
        wchar_t fName[MAX_PATH];
        StringCchCopyNW(fName, ARRAY_SIZE(fName), pServerPB->u.fLfn.fName, pServerPB->u.fLfn.fNameLength/sizeof(wchar_t));
        LOG_VERBOSE(FOLDERSHARING, "ServerMkDir() - fName='%S'", fName);
    }

    // Capture the path name to create
    UInt16 kError;
    wchar_t FileName[MAX_PATH];
    kError = pThis->GetWin32Path(pServerPB->u.fLfn.fName, pServerPB->u.fLfn.fNameLength,
                                FileName, ARRAY_SIZE(FileName));
    if (kError != kErrorNoError) {
        return kError;
    }

    if (!CreateDirectoryW(FileName, NULL)) {
        return kErrorFromLastError();
    }

    LOG_VERBOSE(FOLDERSHARING, "ServerMkDir() - success");
    return kErrorNoError;
}

// ServerRmDir - remove a directory (must be empty)
//
//Parameters:
//    In:     inPB            - Ptr to a Server Parameter block
//            ->fName         - the name of the dir to remove
//    Out:    inPB            - Info returned in the parameter block
//            ->fResult       - Error code
unsigned __int32 __fastcall IOFolderSharing::AsyncServerRmDir(class IOFolderSharing *pThis, PServerPB pServerPB)
{
    if (IsFolderSharingLoggingEnabled(VERBOSE)) {
        wchar_t fName[MAX_PATH];
        StringCchCopyNW(fName, ARRAY_SIZE(fName), pServerPB->u.fLfn.fName, pServerPB->u.fLfn.fNameLength/sizeof(wchar_t));

        LOG_VERBOSE(FOLDERSHARING, "ServerRmDir() - fName='%S'", fName);
    }

    // Capture the path name to create
    UInt16 kError;
    wchar_t FileName[MAX_PATH];
    kError = pThis->GetWin32Path(pServerPB->u.fLfn.fName, pServerPB->u.fLfn.fNameLength,
                                FileName, ARRAY_SIZE(FileName));
    if (kError != kErrorNoError) {
        return kError;
    }

    if (!RemoveDirectoryW(FileName)) {
        return kErrorFromLastError();
    }

    LOG_VERBOSE(FOLDERSHARING, "ServerRmDir() - success");
    return kErrorNoError;
}

// ServerSetAttributes() - set file attibutes and optionally its date/time
//
//Parameters:
//    In:     inPB            - Ptr to a Server Parameter block
//            ->u.fXXX.fName  - the directory name
//            ->fFileAttributes - New attributes
//            ->fFileTimeDate - New Time & Date or NULL if no change in time and date
//    Out:    inPB            - Info returned in the parameter block
//            ->fResult       - Error code
unsigned __int32 __fastcall IOFolderSharing::AsyncServerSetAttributes(class IOFolderSharing *pThis, PServerPB pServerPB)
{
    if (IsFolderSharingLoggingEnabled(VERBOSE)) {
        wchar_t fName[MAX_PATH]; fName[0] = 0;
        StringCchCopyNW(fName, ARRAY_SIZE(fName), pServerPB->u.fLfn.fName, pServerPB->u.fLfn.fNameLength/sizeof(wchar_t));

        LOG_VERBOSE(FOLDERSHARING, "ServerSetAttributes() - fName='%S', fFileAttributes=0x%x, fFileTimeDate=0x%x",
            fName,
            pServerPB->fFileAttributes,
            pServerPB->fFileTimeDate);
    }

    // Capture the path name to set the attributes on
    UInt16 kError;
    wchar_t FileName[MAX_PATH];
    kError = pThis->GetWin32Path(pServerPB->u.fLfn.fName, pServerPB->u.fLfn.fNameLength,
                                FileName, ARRAY_SIZE(FileName));
    if (kError != kErrorNoError) {
        return kError;
    }

    // Set the file attributes
    if (!SetFileAttributes(FileName, pServerPB->fFileAttributes)) {
        return kErrorFromLastError();
    }

    // Set the new file date and time, if present
    unsigned __int32 fFileDateTime = pServerPB->fFileTimeDate;
    if (fFileDateTime) {
        FILETIME ft;

        if (!pThis->LongToFiletime(pServerPB->fFileTimeDate, &ft)) {
            return kErrorGeneralFailure;
        }

        HANDLE hFile;
        hFile = CreateFile(FileName, 
                           FILE_WRITE_ATTRIBUTES,
                           FILE_SHARE_DELETE|FILE_SHARE_READ|FILE_SHARE_WRITE,
                           NULL,
                           OPEN_EXISTING,
                           FILE_FLAG_OPEN_NO_RECALL,
                           NULL);
        if (hFile == INVALID_HANDLE_VALUE) {
            return kErrorFromLastError();
        }
        if (!SetFileTime(hFile, NULL, NULL, &ft)) {
            kError = kErrorFromLastError();
            CloseHandle(hFile);
            return kError;
        }
        CloseHandle(hFile);
    }

    LOG_VERBOSE(FOLDERSHARING, "ServerSetAttributes() - success");
    return kErrorNoError;
}

// ServerRename - rename a file/directory
//
//Parameters:
//    In:     inPB            - Ptr to a Server Parameter block
//            ->u.fXXX.fName  - the name
//            ->u.fXXX.fName2 - the new name
//    Out:    inPB            - Info returned in the parameter block
//            ->fResult       - Error code
unsigned __int32 __fastcall IOFolderSharing::AsyncServerRename(class IOFolderSharing *pThis, PServerPB pServerPB)
{
    if (IsFolderSharingLoggingEnabled(VERBOSE)) {
        wchar_t fName[MAX_PATH];  fName[0] = 0;
        wchar_t fName2[MAX_PATH]; fName2[0] = 0;
        StringCchCopyNW(fName, ARRAY_SIZE(fName), pServerPB->u.fLfn.fName, pServerPB->u.fLfn.fNameLength/sizeof(wchar_t));
        StringCchCopyNW(fName2, ARRAY_SIZE(fName2), pServerPB->u.fLfn.fName2, pServerPB->u.fLfn.fName2Length/sizeof(wchar_t));

        LOG_VERBOSE(FOLDERSHARING, "ServerRename() - from '%S' to '%S'", fName, fName2);
    }

    wchar_t OldName[MAX_PATH];
    wchar_t NewName[MAX_PATH];

    UInt16 kError;
    kError = pThis->GetWin32Path(pServerPB->u.fLfn.fName, pServerPB->u.fLfn.fNameLength,
                                OldName, ARRAY_SIZE(OldName));
    if (kError != kErrorNoError) {
        return kError;
    }

    kError = pThis->GetWin32Path(pServerPB->u.fLfn.fName2, pServerPB->u.fLfn.fName2Length,
                                NewName, ARRAY_SIZE(NewName));
    if (kError != kErrorNoError) {
        return kError;
    }

    // Allow cross-volume copy, in case the shared folder is composed of more than one
    // Windows volume.
    if (!MoveFileEx(OldName, NewName, MOVEFILE_COPY_ALLOWED)) {
        return kErrorFromLastError();
    }

    LOG_VERBOSE(FOLDERSHARING, "ServerRename() - success");
    return kErrorNoError;
}

// ServerDelete - delete a file
//
//Parameters:
//    In:     inPB            - Ptr to a Server Parameter block
//            ->u.fXXX.fName  - the name
//    Out:    inPB            - Info returned in the parameter block
//            ->fResult       - Error code
unsigned __int32 __fastcall IOFolderSharing::AsyncServerDelete(class IOFolderSharing *pThis, PServerPB pServerPB)
{
    if (IsFolderSharingLoggingEnabled(VERBOSE)) {
        wchar_t fName[MAX_PATH];
        StringCchCopyNW(fName, ARRAY_SIZE(fName), pServerPB->u.fLfn.fName, pServerPB->u.fLfn.fNameLength/sizeof(wchar_t));

        LOG_VERBOSE(FOLDERSHARING, "ServerDelete() - fName='%S'", fName);
    }

    wchar_t FileName[MAX_PATH];
    UInt16 kError;
    kError = pThis->GetWin32Path(pServerPB->u.fLfn.fName, pServerPB->u.fLfn.fNameLength,
                                FileName, ARRAY_SIZE(FileName));
    if (kError != kErrorNoError) {
        return kError;
    }

    if (!DeleteFile(FileName)) {
        return kErrorFromLastError();
    }

    LOG_VERBOSE(FOLDERSHARING, "ServerDelete() - success");
    return kErrorNoError;
}

// Return the maximum support file I/O size
void __fastcall IOFolderSharing::ServerGetMaxIOSize(void)
{
    LOG_VERBOSE(FOLDERSHARING, "ServerGetMaxIOSize - returning 0x%x", kFolderSharingMaxReadWriteSize);
    SignalIOComplete(kFolderSharingMaxReadWriteSize);
}

// Queue up an async call to run on the worker thread.  The queue
// supports only one pending operation, with last-writer-wins.
void IOFolderSharing::AsyncCall(pfnAsyncRequest_t pfn)
{
    ASSERT(pfnAsyncRequest == NULL);
    pfnAsyncRequest = pfn;
    CompletionPort.QueueWorkitem(&Callback);
}

// Called by the IO Completion port threadpool
//   lpParameter = this pointer
//   dwBytesTransferred = 0
//   lpOverlapped = 0
void IOFolderSharing::CompletionRoutineStatic(LPVOID lpParameter, DWORD dwBytesTransferred, LPOVERLAPPED lpOverlapped)
{
    IOFolderSharing *pThis = (IOFolderSharing *)lpParameter;

    pThis->CompletionRoutine();
}

// Called by the IO Completion port threadpool
void IOFolderSharing::CompletionRoutine()
{
    // Copy pfnAsyncRequest into a local and zero it out before making the call
    pfnAsyncRequest_t pfn = pfnAsyncRequest;
    pfnAsyncRequest = NULL;

    EnterCriticalSection(&FolderShareLock);
    if (fNeedsReconfiguration) {
        fNeedsReconfiguration = false;
        // Release the lock: AsyncReconfigure needs to acquire the IOLock before re-acquiring the FolderShareLock
        LeaveCriticalSection(&FolderShareLock);
        AsyncReconfigure();
        return;
    }

    if (pfn) {
        // call the async request routine
        unsigned __int32 Result = pfn(this, GetServerPB());

        if (IsFolderSharingLoggingEnabled(VERBOSE)) {
            switch (Result) {
            case kErrorNoError:
                // The Async functions themselves log in the kErrorNoError path, so
                // there is no need to log again here.
                break;
            case kErrorInvalidFunction:
                LOG_VERBOSE(FOLDERSHARING, "Server call failed:  kErrorInvalidFunction");
                break;
            case kErrorFileNotFound:
                LOG_VERBOSE(FOLDERSHARING, "Server call failed:  kErrorFileNotFound");
                break;
            case kErrorPathNotFound:
                LOG_VERBOSE(FOLDERSHARING, "Server call failed:  kErrorPathNotFound");
                break;
            case kErrorTooManyOpenFiles:
                LOG_VERBOSE(FOLDERSHARING, "Server call failed:  kErrorTooManyOpenFiles");
                break;
            case kErrorAccessDenied:
                LOG_VERBOSE(FOLDERSHARING, "Server call failed:  kErrorAccessDenied");
                break;
            case kErrorInvalidHandle:
                LOG_VERBOSE(FOLDERSHARING, "Server call failed:  kErrorInvalidHandle");
                break;
            case kErrorNotSameDevice:
                LOG_VERBOSE(FOLDERSHARING, "Server call failed:  kErrorNotSameDevice");
                break;
            case kErrorNoMoreFiles:
                LOG_VERBOSE(FOLDERSHARING, "Server call failed:  kErrorNoMoreFiles");
                break;
            case kErrorWriteProtect:
                LOG_VERBOSE(FOLDERSHARING, "Server call failed:  kErrorWriteProtect");
                break;
            case kErrorWriteFault:
                LOG_VERBOSE(FOLDERSHARING, "Server call failed:  kErrorWriteFault");
                break;
            case kErrorReadFault:
                LOG_VERBOSE(FOLDERSHARING, "Server call failed:  kErrorReadFault");
                break;
            case kErrorGeneralFailure:
                LOG_VERBOSE(FOLDERSHARING, "Server call failed:  kErrorGeneralFailure");
                break;
            case kErrorSharingViolation:
                LOG_VERBOSE(FOLDERSHARING, "Server call failed:  kErrorSharingViolation");
                break;
            case kErrorLockViolation:
                LOG_VERBOSE(FOLDERSHARING, "Server call failed:  kErrorLockViolation");
                break;
            case kErrorSharingBufferExceeded:
                LOG_VERBOSE(FOLDERSHARING, "Server call failed:  kErrorSharingBufferExceeded");
                break;
            case kErrorDiskFull:
                LOG_VERBOSE(FOLDERSHARING, "Server call failed:  kErrorDiskFull");
                break;
            case kErrorFileExists:
                LOG_VERBOSE(FOLDERSHARING, "Server call failed:  kErrorFileExists");
                break;
            case kErrorInvalidName:
                LOG_VERBOSE(FOLDERSHARING, "Server call failed:  kErrorInvalidName");
                break;
            default:
                LOG_VERBOSE(FOLDERSHARING, "Server call failed:  unknown error 0x%x", Result);
                break;
            }
        }

        // Report the IO as completed
        LeaveCriticalSection(&FolderShareLock); // This must be done before signalling, to avoid deadlock
        SignalIOComplete(Result);  
    }
}

// Indicate that an async I/O has completed.
//
// This may be called from the worker thread without the IOLock held
void IOFolderSharing::SignalIOComplete(unsigned __int32 ResultValue)
{
    EnterCriticalSection(&IOLock);
    IOPending = IOPending ? IOPending - 1 : 0;
    if ( IOPending == 0 )
        SetEvent(hIOComplete);
    Result = ResultValue;
    LeaveCriticalSection(&IOLock);
}

