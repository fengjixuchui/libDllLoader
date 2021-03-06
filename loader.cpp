/*
 * loader.cpp
 *
 *  Created on: 02.11.2011
 *      Author: Kai Uwe Jesussek
 */

#include <windows.h>
#include <tlhelp32.h>
#include <shlwapi.h>
#include "loader.h"
#include "libUnicodeHelper/helper.h"
#include "NinjectLib/IATModifier.h"

#include <iostream>
using namespace std;

#define WIN32_LEAN_AND_MEAN
#define CREATE_THREAD_ACCESS (PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ)

extern "C" BOOL CLASS_DECLSPEC FileExists(const wchar_t *fileName)
{
    DWORD       fileAttr;

    fileAttr = GetFileAttributesW(fileName);
    if (0xFFFFFFFF == fileAttr)
        return FALSE;
    return TRUE;
}
extern "C" BOOL CLASS_DECLSPEC inject_dll_new_process(const char* exe, const char* params, const char* dll, const char * workingDir) {
	bool ret = FALSE;
	STARTUPINFO sInfo = {0};
	sInfo.cb = sizeof(STARTUPINFO);
	PROCESS_INFORMATION pInfo;

	if (CreateProcessA(exe, (char*) params, NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, workingDir, &sInfo, &pInfo))
	{
		try
		{
			Process process(pInfo.dwProcessId);
			IATModifier iatModifier(process);
			// retrieve image base address so IATModifier is able to find the import directory
			iatModifier.setImageBase(process.getImageBase(pInfo.hThread));
			// modify import directory so our injected dll is loaded first
			iatModifier.writeIAT(dll);
			ResumeThread(pInfo.hThread);
			ret = TRUE;
		}
		catch (std::exception& e)
		{
			ResumeThread(pInfo.hThread);
			CloseHandle(pInfo.hThread);
			CloseHandle(pInfo.hProcess);
		}
	}
	return ret;
}

extern "C" BOOL CLASS_DECLSPEC inject_dll_into_processid(DWORD pID, const wchar_t * wDLL_NAME)
{
   LPSTR DLL_NAME;
   UnicodeToAnsi(wDLL_NAME, &DLL_NAME);

   HANDLE Proc;
   LPVOID RemoteString, LoadLibAddy;

   if(!pID)
      return FALSE;

   Proc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pID);
   if(!Proc)
   {
      return FALSE;
   }

   LoadLibAddy = (LPVOID)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryA");
   RemoteString = (LPVOID)VirtualAllocEx(Proc, NULL, strlen(DLL_NAME), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
   WriteProcessMemory(Proc, (LPVOID)RemoteString, DLL_NAME, strlen(DLL_NAME), NULL);
   HANDLE hinject_dll_into_processid = CreateRemoteThread(Proc, NULL, NULL, (LPTHREAD_START_ROUTINE)LoadLibAddy, (LPVOID)RemoteString, NULL, NULL);
   WaitForSingleObject(hinject_dll_into_processid, INFINITE);
   VirtualFreeEx(Proc, RemoteString, strlen(DLL_NAME), MEM_RELEASE);
   CloseHandle(Proc);
   CoTaskMemFree(DLL_NAME);
   return TRUE;
}

extern "C" DWORD GetTargetThreadIDFromProcName(const wchar_t * ProcName)
{
   PROCESSENTRY32W pe;
   HANDLE thSnapShot;
   BOOL retval;
   thSnapShot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
   if(thSnapShot == INVALID_HANDLE_VALUE)
   {
      return FALSE;
   }

   pe.dwSize = sizeof(PROCESSENTRY32W);

   retval = Process32FirstW(thSnapShot, &pe);

   while(retval)
   {
      if(StrStrIW(pe.szExeFile, ProcName))
      {
         return pe.th32ProcessID;
      }
      retval = Process32NextW(thSnapShot, &pe);
   }
   //cout << "[-] could not find process" << endl;
   return 0;
}

extern "C" BOOL CLASS_DECLSPEC inject_dll_into_processname(const wchar_t* processname, const wchar_t * DLL_NAME)
{
	DWORD pID = GetTargetThreadIDFromProcName(processname);
	wchar_t buf[MAX_PATH] = { 0 };
	GetFullPathNameW(DLL_NAME, MAX_PATH, buf, NULL);

	if (!FileExists(buf)) {
		return FALSE;
	}

	if (!inject_dll_into_processid(pID, buf)) {
		return FALSE;
	}
	return TRUE;
}

extern "C" BOOL CLASS_DECLSPEC inject_dll_into_processnameA(const char* processname, const char * DLL_NAME) {
	   LPWSTR w_dllname;
	   LPWSTR w_processname;
	   AnsiToUnicode(DLL_NAME, &w_dllname);
	   AnsiToUnicode(processname, &w_processname);
	   BOOL ret = inject_dll_into_processname(w_processname, w_dllname);
	   CoTaskMemFree(w_dllname);
	   CoTaskMemFree(w_processname);
	   return ret;
}
extern "C" BOOL CLASS_DECLSPEC inject_dll_into_processidA(DWORD pID, const char * DLL_NAME) {
	   LPWSTR w_dllname;
	   AnsiToUnicode(DLL_NAME, &w_dllname);
	   BOOL ret = inject_dll_into_processid(pID, w_dllname);
	   CoTaskMemFree(w_dllname);
	   return ret;
}
