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
#include "cpu.h"
#include "state.h"
#include "board.h"
#include <io.h>
#include <fcntl.h>
#include <ShellAPI.h> // for CommandLineToArgvW()
#include "Htmlhelp.h" // for HelpHtml()
#include "resource.h"

// Include the logging infrastructure
#include "dev_emulator_log.h"
#define TMH_FILENAME "main.tmh"
#include "vsd_logging_inc.h"

#if FEATURE_SKIN
#include <new> // for nothrow
#include "XMLSkin.h"
#endif

#if FEATURE_GUI
#include "WinController.h"
#endif

// Install the failure hook for delay loading
#include <delayimp.h>
FARPROC WINAPI delayLoadFailureHook(unsigned dliNotify, PDelayLoadInfo pdli);
// Delay loading hook
PfnDliHook __pfnDliFailureHook2 = delayLoadFailureHook;

/*
    These two defines allow two emulator processes to run side-by-side, with one confirming that its behavior is
    the same as the others.  Here is how to use the feature:
    1.  Uncomment GOOD_EMULATOR, and build an emulator.exe with LOGGING_ENABLED.  This is the known-good emulator.  Copy
        it to a temporary directory.
    2.  Comment out GOOD_EMULATOR and uncomment BAD_EMULATOR, and build an emulator.exe with LOGGING_ENABLED>  This is
        the known-bad emulator.
    3.  Run the known-good emulator - it will load the .bin file then pause until it hears from the "bad" emulator.
    4.  Run the known-bad emulator under a debugger.  After each emulated instruction, it allows the "good" emulator to
        advance one instruction, then the "bad" diffs the CPU structure and InstructionPointer value with the "bad".
        Any differences trigger a debug message and a DebugBreak() call.

    Note that this side-by-side scheme works only up to the first interrupt delivery, then the two emulators
    quickly diverge and never converge again.  You'll know when this happens if one of the InstructionPointer
    value is 0xffff????.
*/
#if LOGGING_ENABLED
unsigned __int32 InstructionCount=0;

bool LogToFile=false;

// Set this to the number of instructions to execute before enabling logging.
unsigned __int32 LogInstructionStart = 0xffffffff;

unsigned __int32 FileCount=0;
char fname[64];
FILE *output = stdout;
#endif //LOGGING_ENABLED

bool bSaveState = false;
bool bIsRestoringState = false;
bool fIsRunning;
EmulatorConfig Configuration;

CRITICAL_SECTION IOLock; // used to serialize access to IO devices from Win32 worker threads
HANDLE hIdleEvent;
PROCESSOR_CONFIG ProcessorConfig;
#if defined(FEATURE_COM_INTERFACE)
HRESULT StartCOM(void);
HRESULT RegisterVMinROT(bool RegisterInROT);
bool RegisterVMIDAsRunning(bool RegisterInROT);
#endif

bool RedirectStdHandle(FILE *stdHandle, int Win32Name)
{
    int i;
    HANDLE h;
    FILE *fp;

    h = GetStdHandle(Win32Name);
    if (h == INVALID_HANDLE_VALUE || h == NULL) {
        return false;
    }
    i=_open_osfhandle((intptr_t)h, (stdHandle == stdin) ? _O_RDONLY : _O_WRONLY);
    if (i == -1) {
        return false;
    }
    fp = _fdopen(i, (stdHandle == stdin) ? "r" : "w");
    if (fp == NULL) {
        _close(i);
        return false;
    }
    memcpy(stdHandle, fp, sizeof(FILE));
    return true;
}

bool CreateConsoleWindow(void)
{
    if (!Configuration.fCreateConsoleWindow) {
        return true; // no console window requested
    }
    if (AllocConsole() == FALSE) {
        // a console window already exists for the process.
    } else {
        // a new console has been created, with new HANDLEs
        // for standard out, in, and error.
        if (!RedirectStdHandle(stdin, STD_INPUT_HANDLE)) {
            return false;
        }
        if (!RedirectStdHandle(stdout, STD_OUTPUT_HANDLE)) {
            return false;
        }
        if (!RedirectStdHandle(stderr, STD_ERROR_HANDLE)) {
            return false;
        }
    }
    return true;
}

bool LoadImageOrRestoreState(void)
{
    if (Configuration.UseDefaultSaveState) {
        bIsRestoringState = true;
        if ( BoardLoadSavedState(true) )
            return true;
        bIsRestoringState = false;
    }
    if (Configuration.getLoadImage() ) {
        // Disable prompting on power on for unsafe devices on cold boot
        Configuration.NoSecurityPrompt = true;
        return BoardLoadImage(Configuration.getROMImageName());
    } else if (Configuration.isSaveStateEnabled()) {
        bIsRestoringState = true;
        return BoardLoadSavedState(false);
    }
    // Else no ROM image and save-state not enabled
    ShowDialog(ID_MESSAGE_MUST_SPECIFY_BIN_SAVESTATE);
    return false;
}

// Unregister the logging provider GUID and stop code markers
void __cdecl UninitializeLogging(void)
{
    // Stop Code Markers
    UninitializePerformanceDLL( DEVEMULATORPERF ); 
    // Deregister logging provider
    WPP_CLEANUP();
}

void __cdecl newhandler( )
{
    TerminateWithMessage(ID_MESSAGE_RESOURCE_EXHAUSTED);
}


int __stdcall wWinMain(
                      HINSTANCE hInstance,
                      HINSTANCE hPrevInstance,
                      __inout_z LPWSTR lpCmdLine,
                      int nCmdShow)
{
    int argc;
    wchar_t **argv;

    // Don't block on critical error dialogs or file-open errors (such as specifying a
    // Windows filename like b:\savestate.dat).
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);

#if FEATURE_SKIN
    std::set_new_handler(newhandler);
#endif

    // Start Code Markers
    InitPerformanceDLL( DEVEMULATORPERF, false ); 

    // Initialize WMI based logging
    WPP_INIT_TRACING(L"Microsoft\\VSD\\DeviceEmulator");
    atexit(UninitializeLogging);

    LOG_INFO(GENERAL, "DeviceEmulator started with the following - %S", lpCmdLine );

    // if lpCmdLine is "", CommandLineToArgvW() returns argc==1 and argv[0]==path to
    // the EXE.  Take care of that special case first.
    if (*lpCmdLine==L'\0') {
        BoardPrintUsage();
        goto ErrorExit;
    }

    // Parse the new command line 
    wchar_t * updatedCommandLine = RemoveEscapes( lpCmdLine );
    if ( updatedCommandLine == NULL ) {
        goto ErrorExit;
    }
    argv = CommandLineToArgvW(updatedCommandLine, &argc);
    delete [] updatedCommandLine;
    if (argv == NULL) {
        goto ErrorExit;
    }

    if (argc < 1) {
        BoardPrintUsage();
        goto ErrorExit;
    }

    InitializeCriticalSection(&IOLock);
    hIdleEvent = CreateEvent(NULL, FALSE, FALSE, NULL); // create auto-reset event, initially signalled
    if (hIdleEvent == NULL || !CpuReset()) {
        goto ErrorExit;
    }

    if (!Configuration.init()) {
        goto ErrorExit;
    }

    ParseErrorStruct ParseError;
    if (!BoardParseCommandLine(argc, argv, &Configuration, &ParseError)) {
        if ( ParseError.ParseError != 0 )
            BoardPrintErrorAndUsage( &ParseError );
        else
            BoardPrintUsage();
        goto ErrorExit;
    }

#if FEATURE_COM_INTERFACE
    HRESULT hr = StartCOM();
    if (FAILED(hr)) {
        goto ErrorExit;
    }
    // If this emulator is not meant to power up - block forever after registering in ROT
    if (Configuration.DialogOnly) {
        if (!RegisterVMIDAsRunning(true)) {
            goto ErrorExit;
        }
        // We will be terminated via the COM interface
        while( true )
        {
            Sleep(1000000);
        }
    }
#endif //!FEATURE_COM_INTERFACE

    if (!LoadImageOrRestoreState()) {
        BoardPrintUsage();
        goto ErrorExit;
    }


    // At this point, the emulator can begin making decisions based on
    // values within the Configuration object.  Before this line,
    // the contents of Configuration may still be changing due to
    // restore-state, COM activation, etc.

#if LOGGING_ENABLED
    if(LogToFile){
        if (FAILED(StringCchPrintfA(fname, ARRAY_SIZE(fname), "\\jit-output%d.txt",FileCount))) {
            ShowDialog(ID_MESSAGE_UNABLE_TO_OPEN,fname);
            goto ErrorExit;
        }
        if( fopen_s(&output,fname,"w")){
            ShowDialog(ID_MESSAGE_UNABLE_TO_OPEN,fname);
            goto ErrorExit;
        }
    }
#endif

    if (!CreateConsoleWindow()) {
        goto ErrorExit;
    }

#if defined(FEATURE_COM_INTERFACE)
    // This must be done before power-on, as the Configuration.VMID value
    // is used during power-on of the network adapter devices. At this
    // point we do not add Configuration.VMID to the ROT as that is used 
    // to indicate successful PowerOn.
    if (!RegisterVMIDAsRunning(false)) {
        ShowDialog(ID_MESSAGE_VMID_IN_USE);
        goto ErrorExit;
    }
#endif // FEATURE_COM_INTERFACE

    CodeMarker(perfEmulatorBootBegin);

    if(!BoardPowerOn()){
        ShowDialog(ID_MESSAGE_POWERON_ERROR);
        goto ErrorExit;
    }

#if FEATURE_COM_INTERFACE
    if (FAILED(RegisterVMinROT(true)))
        goto ErrorExit;
#endif
    fIsRunning = true;
    CpuSimulate();

ErrorExit:
    exit(1); 
}

#if !FEATURE_GUI
int __cdecl wmain(int argc, __inout_ecount(argc) wchar_t *argv[])
{
    LPWSTR lpCommandLine = GetCommandLineW();

    // Skip past the first argument, which is the name of the EXE.  wWinMain() doesn't expect it.
    while(*lpCommandLine != ' ' && *lpCommandLine != '\0') {
        if (*lpCommandLine == '\"') {
            // Seek ahead to the matching close-quote
            do {
                lpCommandLine++;
            } while (*lpCommandLine != '\"' && *lpCommandLine != '\0');
            if (*lpCommandLine == '\0') {
                break;
            }
        }
        lpCommandLine++;
    }
    while (*lpCommandLine == ' ') {
        lpCommandLine++;
    }

    return wWinMain(NULL, NULL, lpCommandLine, 0);
}
#endif


void __fastcall AssertionFailed(const char *Expression, const char *FileName, const int LineNumber)
{
    wchar_t Buffer[512];
    int Response;

    if (FAILED(StringCchPrintfW(Buffer, ARRAY_SIZE(Buffer),
               L"ASSERTION FAILED '%S' in file %S at line %d.  Debug?\n", 
               Expression, FileName, LineNumber)))
    {
        TerminateWithMessage(ID_MESSAGE_RESOURCE_EXHAUSTED);
    }
    Response = MessageBoxW(NULL, Buffer, EMULATOR_NAME_W, MB_YESNO);
    if (Response == IDYES) {
        DebugBreak();
    }
}

__declspec(noreturn) void __fastcall TerminateWithMessage(unsigned __int32 MsgID)
{
    ShowDialog(MsgID);
    exit(1);
}

int __cdecl ShowDialogMain(unsigned int Expression, unsigned int DialogType, va_list* Arguments)
{
    // Check if we should not display the error messages
    if ( Configuration.SurpressMessages || Configuration.VSSetup)
        return IDCANCEL;

    // Release the lock here to avoid holding it while SendMessage is called by
    // MessageBox
    bool HoldingIOLock = false;
    if ( HandleToUlong((IOLock).OwningThread) == GetCurrentThreadId() )
    {
        HoldingIOLock = true;
        LeaveCriticalSection(&IOLock);
    }
    wchar_t Buffer[4*1024];

    // Load the string from the satellite DLL
    wchar_t stringBuffer[MAX_LOADSTRING];
    if (!Configuration.Resources.getString(Expression, stringBuffer ))
    {
        if (!Configuration.Resources.getString(ID_MESSAGE_INTERNAL_ERROR, stringBuffer ))
            wcscpy_s(stringBuffer, MAX_LOADSTRING, L"Error: System resources exhausted.");
        DialogType = MB_OK;
    }

    // Try to format the output string using the format string from resources DLL
    if ( !FormatMessage( FORMAT_MESSAGE_FROM_STRING, stringBuffer, 0, 0, Buffer, ARRAY_SIZE(Buffer), Arguments))
    {
        // We failed to format the error string - possible because it is too long
        // Load the internal error string instead
        if (!Configuration.Resources.getString(ID_MESSAGE_INTERNAL_ERROR, Buffer ))
            wcscpy_s(Buffer, MAX_LOADSTRING, L"Error: System resources exhausted.");
        DialogType = MB_OK;
    }

    // Force the NULL termination of the buffer in the error case
    Buffer[ARRAY_SIZE(Buffer)-1] = L'\0';

    int result = MessageBox(
#if FEATURE_GUI
            (WinCtrlInstance ? (WinCtrlInstance->GetWincontrollerHWND()) : NULL), 
#else //!FEATURE_GUI
            NULL,
#endif //!FEATURE_GUI
            Buffer, EMULATOR_NAME_W, DialogType);

    if ( HoldingIOLock )
        EnterCriticalSection(&IOLock);

    return result;
}

void __cdecl ShowDialog(unsigned int Expression, ...)
{
    va_list l;
    va_start(l, Expression);
    (void)ShowDialogMain(Expression, MB_OK, &l);
    va_end(l);
}

bool __cdecl PromptToAllow(unsigned int Expression, ...)
{
    va_list l;
    va_start(l, Expression);
    int result = ShowDialogMain(Expression, MB_YESNO, &l);
    va_end(l);
    return (result == IDYES ? true : false );
}

int __cdecl PromptToAllowWithCancel(unsigned int Expression, ...)
{
    va_list l;
    va_start(l, Expression);
    int result = ShowDialogMain(Expression, MB_YESNOCANCEL, &l);
    va_end(l);
    return result;
}


void __fastcall DecodeLastError( unsigned int ErrorCode, int buffSize, __out_ecount(buffSize) wchar_t *stringBuffer)
{
    ASSERT(stringBuffer);
    int flags = FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
    if (!FormatMessage( flags, NULL, ErrorCode, Configuration.Resources.getLanguage(),
                        stringBuffer, buffSize, NULL ))
    {
        if (stringBuffer) 
            stringBuffer[0] = 0;
    }

    return;
}


// wcsicmp, but accepts NULL pointers.
int SafeWcsicmp(const wchar_t *s1, const wchar_t *s2)
{
    if (s1 == NULL) {
        s1 = L"";
    }
    if (s2 == NULL) {
        s2 = L"";
    }
    int ret_val = _wcsicmp(s1, s2);

    if ( ret_val != 0 )
        return 1;
    else
        return 0;
}

wchar_t * RemoveEscapes( __inout_z wchar_t * input )
{
    if ( input == NULL )
        return NULL;
    // Calculate the length of the string
    size_t NewLength = wcslen(input) + 1;
    unsigned int i = 0, j = 0;
    while ( input[i] != L'\0' )
    {
        if (input[i] == '"' )
            NewLength++;
        i++;
    }
    // Allocate the new string
    wchar_t * OutStr = new wchar_t[NewLength];
    if (OutStr == NULL)
        return NULL;
    OutStr[0] = '\0';

    // Copy the input string into the output string replacing \" with \\"
    i = 0;
    while (input[i] != L'\0')
    {
        if (i > 0 && input[i] == '"'&& input[i-1] == '\\' )
        {
            input[i] = L'\0';
            if (FAILED(StringCchCatW(OutStr, NewLength, &input[j])) )
                goto Error;
            if (FAILED(StringCchCatW(OutStr, NewLength, L"\\")))
                goto Error;
            input[i] = L'"';
            j = i;
        }
        i++;
    }
    if ( i != (j + 1) && FAILED(StringCchCatW(OutStr, NewLength, &input[j])))
        goto Error;
    return OutStr;
Error:
    if ( OutStr != NULL )
        delete [] OutStr;
    return NULL;
}

FARPROC WINAPI delayLoadFailureHook(unsigned dliNotify, PDelayLoadInfo pdli)
{
    ASSERT( dliNotify == dliFailLoadLib || dliNotify == dliFailGetProc );

    if ( dliNotify == dliFailLoadLib || dliNotify == dliFailGetProc )
    {
        ShowDialog(ID_MESSAGE_DELAY_LOAD_ERROR, pdli->szDll);
        exit(1);
    }

    return NULL;
}

bool ShowHelp( __inout_z wchar_t * buffer )
{
    wchar_t szBaseDir[MAX_PATH], szLangDir[MAX_PATH], szHelpFile[MAX_PATH];
    bool result = false;
    if (Configuration.Resources.getLocalizedDirectory(Configuration.Resources.getLanguage(), szBaseDir, MAX_PATH, szLangDir, MAX_PATH) == true)
    {
        if (!FAILED(StringCchPrintfW(szHelpFile, MAX_PATH, L"%s\\%s\\%s", szBaseDir, szLangDir, HELP_FILE_W) )) 
        {
            if (HtmlHelp(NULL, szHelpFile, HH_DISPLAY_TOPIC, (DWORD_PTR)buffer) == NULL)
                ShowDialog(ID_MESSAGE_HELP_NOT_FOUND);
            else
                result = true;
        }
        else
            ShowDialog(ID_MESSAGE_INTERNAL_ERROR);
    }
    else
        ShowDialog(ID_MESSAGE_INTERNAL_ERROR);
    if ( buffer != NULL )
        delete [] (wchar_t *)buffer;

    return result;
}
