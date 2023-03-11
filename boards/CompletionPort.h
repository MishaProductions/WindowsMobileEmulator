/*++

 Copyright (c) 2006 Microsoft Corporation.  All rights reserved.

 The use and distribution terms for this software are contained in the file
 named license.rtf, which can be found in the root of this distribution.
 By using this software in any fashion, you are agreeing to be bound by the
 terms of this license.

 You must not remove this notice, or any other, from this software.

--*/

#ifndef COMPLETIONPORT_H__
#define COMPLETIONPORT_H__

typedef void (__fastcall *COMPLETIONPORT_ROUTINE)(LPVOID lpParameter, DWORD dwBytesTransferred, LPOVERLAPPED lpOverlapped);
typedef struct { 
    COMPLETIONPORT_ROUTINE lpRoutine;
    LPVOID lpParameter;
} COMPLETIONPORT_CALLBACK;

class EmulatorCompletionPort {
public:
    EmulatorCompletionPort() { hCompletionPort = NULL; }
    ~EmulatorCompletionPort() { }

    bool Initialize(void);
    bool AssociateHandleWithCompletionPort(HANDLE hFile, COMPLETIONPORT_CALLBACK *pCallback);
    bool QueueWorkitem(COMPLETIONPORT_CALLBACK *pCallback);

private:
    HANDLE hCompletionPort;
    int ThreadCount;
    int MaximumThreadCount;
    int BusyThreadCount;

    static DWORD WINAPI ThreadPoolWorkerStatic(LPVOID lpvThreadParam);
    DWORD WINAPI ThreadPoolWorker();
};

extern class EmulatorCompletionPort CompletionPort;

#endif // COMPLETIONPORT_H__

