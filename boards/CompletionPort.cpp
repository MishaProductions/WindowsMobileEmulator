/*++

 Copyright (c) 2006 Microsoft Corporation.  All rights reserved.

 The use and distribution terms for this software are contained in the file
 named license.rtf, which can be found in the root of this distribution.
 By using this software in any fashion, you are agreeing to be bound by the
 terms of this license.

 You must not remove this notice, or any other, from this software.

--*/

#include "emulator.h"
#include "CompletionPort.h"

class EmulatorCompletionPort CompletionPort;

DWORD EmulatorCompletionPort::ThreadPoolWorkerStatic(LPVOID lpvThreadParam)
{
    return ((EmulatorCompletionPort*)lpvThreadParam)->ThreadPoolWorker();
}

DWORD WINAPI EmulatorCompletionPort::ThreadPoolWorker()
{
    InterlockedIncrement((LONG*)&ThreadCount);

    while (1) {
        BOOL b;
        DWORD dwBytesTransferred;
        COMPLETIONPORT_CALLBACK *pCallback;
        LPOVERLAPPED lpOverlapped;
        int BusyCount;

        b = GetQueuedCompletionStatus(hCompletionPort, 
            &dwBytesTransferred, 
            (ULONG_PTR*)&pCallback, 
            &lpOverlapped, 
            INFINITE);
        if (b == FALSE) {
            // We just dequeued a completion notification for a failed I/O.
            // Note:  we choose not to call pCallback failed I/Os.  No code currently cares.
            continue;
        }

        BusyCount = (int)InterlockedIncrement((LONG*)&BusyThreadCount);
        if (BusyCount == ThreadCount && ThreadCount < MaximumThreadCount) {
            // All threads are currently busy - power up another one
            HANDLE hThread;

            hThread = CreateThread(NULL, 0, ThreadPoolWorkerStatic, this, 0, NULL);
            if (hThread != NULL) {
                CloseHandle(hThread);
            }
            // Ignore failure here - just continue running with the current
            // number of threadpool threads and try again later.
        }
        pCallback->lpRoutine(pCallback->lpParameter, dwBytesTransferred, lpOverlapped);
        InterlockedDecrement((LONG*)&BusyThreadCount);
    }
    return 0;
}

int GetCurrentProcessCpuCount(void)
{
    DWORD_PTR pmask;
    DWORD_PTR smask;

    if (!GetProcessAffinityMask(GetCurrentProcess(), &pmask, &smask)) {
        return 1;
    }

    if (pmask == 1) {
        return 1;
    }

    int count = 0;

    pmask &= smask;
    while (pmask) {
        count += (LONG)(pmask & 1);
        pmask >>= 1;
    }
    return count;
}

// This must be called with the IO lock held
bool EmulatorCompletionPort::Initialize(void)
{
    if (hCompletionPort) {
        return true;
    }

    BusyThreadCount = 0;

    hCompletionPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);

    if (!hCompletionPort) {
        return false;
    }

    int CpuCount = GetCurrentProcessCpuCount();

    int MinimumThreadCount = 2;
    MaximumThreadCount = max(MinimumThreadCount, CpuCount);
    // Consider adding code here to allow the user to override MinimumThreadCount
    // and MaximumThreadCount via registry keys.

    for (int i=0; i<MinimumThreadCount; ++i) {
        HANDLE hThread;

        hThread = CreateThread(NULL, 0, ThreadPoolWorkerStatic, this, 0, NULL);
        if (hThread == NULL) {
            CloseHandle(hCompletionPort);
            hCompletionPort = NULL;
            return false;
        }
        CloseHandle(hThread);
    }
    return true;
}

bool EmulatorCompletionPort::AssociateHandleWithCompletionPort(HANDLE hFile, COMPLETIONPORT_CALLBACK *pCallback)
{
    HANDLE hAssociatedPort;
    hAssociatedPort = CreateIoCompletionPort(hFile, hCompletionPort, (ULONG_PTR)pCallback, 0);
    if (hAssociatedPort) {
        if ( hAssociatedPort != hCompletionPort )
        {
            // This should never happen, make sure we don't leak a handle and report error
            ASSERT(false);
            CloseHandle(hAssociatedPort);
            return false;
        }
        return true;
    } else {
        return false;
    }
}

bool EmulatorCompletionPort::QueueWorkitem(COMPLETIONPORT_CALLBACK *pCallback)
{
    BOOL b = PostQueuedCompletionStatus(hCompletionPort, 0, (ULONG_PTR)pCallback, NULL);

    if (b == FALSE) {
        return false;
    }
    return true;
}
