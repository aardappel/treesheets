/**********************************************************************
 *
 * main.cpp
 *
 *
 * History:
 *  2008-11-27   v1    - Header added
 *                       Samples for Exception-Crashes added...
 *  2009-11-01   v2    - Moved to stackwalker.codeplex.com
 *
 **********************************************************************/

#include <stdio.h>
#include <tchar.h>
#include "StackWalker\stackwalker.h"

static TCHAR s_szExceptionLogFileName[_MAX_PATH] = _T("\\exceptions.log");  // default
static BOOL s_bUnhandledExeptionFilterSet = FALSE;
static int argc = 0;
static char **argv = nullptr;

// Specialized stackwalker-output classes
class CustomStackWalker : public StackWalker {
    public:
    virtual void OnOutput(LPCSTR szText) {
        auto hAppend = CreateFile(TEXT(s_szExceptionLogFileName),
                                  FILE_APPEND_DATA,       // open for writing
                                  FILE_SHARE_READ,        // allow multiple readers
                                  NULL,                   // no security
                                  OPEN_ALWAYS,            // open or create
                                  FILE_ATTRIBUTE_NORMAL,  // normal file
                                  NULL);                  // no attr. template
        WriteFile(hAppend, szText, strlen(szText), nullptr, nullptr);
        CloseHandle(hAppend);
    }

    // Don't care about all the module ouput.
    void OnLoadModule(LPCSTR, LPCSTR, DWORD64, DWORD, DWORD, LPCSTR, LPCSTR, ULONGLONG) {}
};

// For more info about "PreventSetUnhandledExceptionFilter" see:
// "SetUnhandledExceptionFilter" and VC8
// http://blog.kalmbachnet.de/?postid=75
// and
// Unhandled exceptions in VC8 and above… for x86 and x64
// http://blog.kalmbach-software.de/2008/04/02/unhandled-exceptions-in-vc8-and-above-for-x86-and-x64/
// Even better:
// http://blog.kalmbach-software.de/2013/05/23/improvedpreventsetunhandledexceptionfilter/

#if defined _M_X64 || defined _M_IX86
static BOOL PreventSetUnhandledExceptionFilter() {
    HMODULE hKernel32 = LoadLibrary(_T("kernel32.dll"));
    if (hKernel32 == NULL) return FALSE;
    void *pOrgEntry = GetProcAddress(hKernel32, "SetUnhandledExceptionFilter");
    if (pOrgEntry == NULL) return FALSE;

#   ifdef _M_IX86
    // Code for x86:
    // 33 C0                xor         eax,eax
    // C2 04 00             ret         4
    unsigned char szExecute[] = { 0x33, 0xC0, 0xC2, 0x04, 0x00 };
#   elif _M_X64
    // 33 C0                xor         eax,eax
    // C3                   ret
    unsigned char szExecute[] = { 0x33, 0xC0, 0xC3 };
#   else
#        error "The following code only works for x86 and x64!"
#   endif

    DWORD dwOldProtect = 0;
    BOOL bProt =
        VirtualProtect(pOrgEntry, sizeof(szExecute), PAGE_EXECUTE_READWRITE, &dwOldProtect);

    SIZE_T bytesWritten = 0;
    BOOL bRet = WriteProcessMemory(GetCurrentProcess(), pOrgEntry, szExecute, sizeof(szExecute),
                                   &bytesWritten);

    if ((bProt != FALSE) && (dwOldProtect != PAGE_EXECUTE_READWRITE)) {
        DWORD dwBuf;
        VirtualProtect(pOrgEntry, sizeof(szExecute), dwOldProtect, &dwBuf);
    }
    return bRet;
}
#else
#    pragma message("This code works only for x86 and x64!")
#endif

static LONG __stdcall CrashHandlerExceptionFilter(EXCEPTION_POINTERS *pExPtrs) {
#   ifdef _M_IX86
    if (pExPtrs->ExceptionRecord->ExceptionCode == EXCEPTION_STACK_OVERFLOW) {
        static char MyStack[1024 * 128];  // be sure that we have enought space...
        // it assumes that DS and SS are the same!!! (this is the case for Win32)
        // change the stack only if the selectors are the same (this is the case for Win32)
        //__asm push offset MyStack[1024*128];
        //__asm pop esp;
        __asm mov eax, offset MyStack[1024 * 128];
        __asm mov esp, eax;
    }
#   endif

    CustomStackWalker sw;  // output to console
    sw.OnOutput("===== Crash Log =====\n");
    SYSTEMTIME st;
    GetSystemTime(&st);
    const size_t buflen = 1024;
    TCHAR lString[buflen] = { 0 };
    sprintf_s(lString, buflen, _T("Date: %d-%02d-%02d, Time: %02d:%02d:%02d\n"),
                st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    sw.OnOutput(lString);
    sw.OnOutput("CMD: ");
    for (int i = 0; i < argc; i++) {
        sw.OnOutput(argv[i]);
        sw.OnOutput(" ");
    }
    sw.OnOutput("\n");
    sprintf_s(lString, buflen, _T("ExpCode: 0x%8.8X, ExpFlags: %d, ExpAddress: 0x%8.8X\n"),
                pExPtrs->ExceptionRecord->ExceptionCode, pExPtrs->ExceptionRecord->ExceptionFlags,
                pExPtrs->ExceptionRecord->ExceptionAddress);
    sw.OnOutput(lString);
    sw.ShowCallstack(GetCurrentThread(), pExPtrs->ContextRecord);
    sw.OnOutput("\n");
    sprintf_s(lString, buflen, _T("Please send %s to the developer!\n"), s_szExceptionLogFileName);
    for (int i = 1; i < argc; i++) {
        strcat_s(lString, buflen, argv[i]);
        strcat_s(lString, buflen, " ");
    }
    strcat_s(lString, buflen, "\n");
    FatalAppExit(-1, lString);
    return EXCEPTION_CONTINUE_SEARCH;
}

void InitUnhandledExceptionFilter(int _argc, char *_argv[]) {
    argc = _argc;
    argv = _argv;
    TCHAR szModName[_MAX_PATH];
    if (GetModuleFileName(NULL, szModName, sizeof(szModName) / sizeof(TCHAR)) != 0) {
        _tcscpy_s(s_szExceptionLogFileName, szModName);
        _tcscat_s(s_szExceptionLogFileName, _T(".exp.log"));
    }
    if (s_bUnhandledExeptionFilterSet == FALSE) {
        // set global exception handler (for handling all unhandled exceptions)
        SetUnhandledExceptionFilter(CrashHandlerExceptionFilter);
#   if defined _M_X64 || defined _M_IX86
        PreventSetUnhandledExceptionFilter();
#   endif
        s_bUnhandledExeptionFilterSet = TRUE;
    }
}
