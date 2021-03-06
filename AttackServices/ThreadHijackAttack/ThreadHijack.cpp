#include <windows.h>
#include <iostream>
#include "ThreadHijack.h"

using namespace std;

int ThreadHijack(HANDLE process, DWORD threadID) {

	FARPROC addrCloseHandle = GetProcAddress(GetModuleHandle(TEXT("kernel32.dll")), "CloseHandle");
	FARPROC addrCreateFileA = GetProcAddress(GetModuleHandle(TEXT("kernel32.dll")), "CreateFileA");
    if (addrCreateFileA == NULL || addrCloseHandle == NULL) {
        cout << "GetProcAddress returned NULL" << endl;
        return 1;
	}
	void* remoteMemory = VirtualAllocEx(process, NULL, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
	if (remoteMemory == NULL) {
		return 2;
	}
	

	BYTE codeCave[] = {
		0x48, 0x83, 0xE4, 0xF0,				// +0 and rsp, 0x0f (make sure stack 16-byte aligned)
		0x48, 0xB9, 0, 0, 0, 0, 0, 0, 0, 0, // +4 mov rcx (lpFileName)
        0x48, 0xBA, 0, 0, 0, 0, 0, 0, 0, 0, // +14 mov rdx (dwDesiredAccess)
        0x4D, 0x31, 0xC0,                   // +24 xor r8,r8 (dwShareMode)
		0x4D, 0x31, 0xC9,                   // +27 xor r9,r9 (lpSecurityAttributes)
		0x48, 0x83, 0xEC, 0x08,             // +30 sub rsp, 0x08 (We are pushing 3 Parameters = 24 byte. Stack must be 16 byte aligned, so we add 8 byte beforehand)
        0x68, 0x00, 0x00, 0x00, 0x00,       // +34 push 0x0 (hTemplateFile)
        0x68, 0x80, 0x00, 0x00, 0x00,       // +39 push 0x80 (dwFlagsAndAttributes)
        0x68, 0x02, 0x00, 0x00, 0x00,       // +44 push 0x2 (dwCreationDisposition)
		0x48, 0xB8, 0, 0, 0, 0, 0, 0, 0, 0, // +49 mov rax (CreateFileA Process Address)
		0x48, 0x83, 0xEC, 0x20,             // +59 sub rsp, 0x20 (save 32 byte for Windows parameters - must be always done)
		0xFF, 0xD0,                         // +63 call rax
		0x48, 0x83, 0xC4, 0x40,				// +65 add rsp, 0x40 (0x20 + 3 Parameters + 8 Byte alignment buf)
		0x48, 0x89, 0xC1,					// +69 mov rcx, rax (HANDLE of CreateFileA)
		0x48, 0xB8, 0, 0, 0, 0, 0, 0, 0, 0, // +72 mov rax (CloseHandle Process Address)
		0x48, 0x83, 0xEC, 0x20,				// sub rsp 0x20
		0xFF, 0xD0,							// call rax
		0x48, 0x83, 0xC4, 0x20,				// add rsp, 0x20
        0xEB, 0xFE                          // nop + jmp rel8 -2
	};
	
	*(DWORD64*)((PUCHAR)codeCave + 6) = (DWORD64)(ULONG_PTR)remoteMemory + sizeof(codeCave);
	*(DWORD64*)((PUCHAR)codeCave + 16) = GENERIC_READ | GENERIC_WRITE;
    *(DWORD64*)((PUCHAR)codeCave + 51) = (DWORD64)(ULONG_PTR)addrCreateFileA;
	*(DWORD64*)((PUCHAR)codeCave + 74) = (DWORD64)(ULONG_PTR)addrCloseHandle;
	
	// const TCHAR filename[] = "hijackConfirmationFile";
	TCHAR filename[MAX_PATH];
    GetTempPath(MAX_PATH, filename);
    lstrcatA(filename, "hijackConfirmationFile");

	WriteProcessMemory(process, remoteMemory, codeCave, sizeof(codeCave), NULL);
	WriteProcessMemory(process, (void*)((DWORD64)remoteMemory + sizeof(codeCave)), &filename, sizeof(filename), NULL);

	// suspend the thread and query its control context
	HANDLE thread = OpenThread((THREAD_GET_CONTEXT | THREAD_SUSPEND_RESUME | THREAD_SET_CONTEXT), false, threadID);
	if (thread == NULL) {
		return 3;
	}
	DWORD ret = SuspendThread(thread);
	if (ret == -1) {
		return 4;
	}

	CONTEXT tcInitial;
	CONTEXT tcHijack;
	CONTEXT tcCurrent;
	tcInitial.ContextFlags = CONTEXT_ALL;
	if (!GetThreadContext(thread, &tcInitial)) {
		return 5;
	}
	CopyMemory(&tcHijack, &tcInitial, sizeof(CONTEXT));
	CopyMemory(&tcCurrent, &tcInitial, sizeof(CONTEXT));

	//hijack the thread
	tcHijack.Rip = (DWORD64)remoteMemory;
	if (!SetThreadContext(thread, &tcHijack)) {
		return 6;
	}
	ret = ResumeThread(thread);
	if (ret == -1) {
		return 7;
	}

	//wait until the code cave did it's job
	DWORD64 addrEndOfExec = (DWORD64)remoteMemory + sizeof(codeCave) - 2;
	do {
		GetThreadContext(thread, &tcCurrent);
	} while (tcCurrent.Rip != addrEndOfExec);
	SuspendThread(thread);
	SetThreadContext(thread, &tcInitial);
	ResumeThread(thread);
	return 0;
}