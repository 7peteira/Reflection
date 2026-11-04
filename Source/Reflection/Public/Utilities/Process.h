#pragma once

#include "HAL/PlatformProcess.h"

#if  !PLATFORM_LINUX
#include "Windows/WindowsHWrapper.h"
#include "Windows/AllowWindowsPlatformTypes.h"
#include <TlHelp32.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

inline void CloseApplicationByProcessName(const FString& ProcessName) {

#if  !PLATFORM_LINUX
	DWORD ProcessID = 0;

	const HANDLE Snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);

	if (Snapshot != INVALID_HANDLE_VALUE) {
		PROCESSENTRY32 ProcessEntry;
		ProcessEntry.dwSize = sizeof(PROCESSENTRY32);

		if (Process32First(Snapshot, &ProcessEntry)) {
			do {
				if (FCString::Stricmp(ProcessEntry.szExeFile, ProcessName.GetCharArray().GetData()) == 0) {
					ProcessID = ProcessEntry.th32ProcessID;
					break;
				}
			} while (Process32Next(Snapshot, &ProcessEntry));
		}

		CloseHandle(Snapshot);
	}

	if (ProcessID != 0) {
		const HANDLE Process = OpenProcess(PROCESS_TERMINATE, false, ProcessID);

		if (Process != nullptr) {
			TerminateProcess(Process, 0);
			CloseHandle(Process);
		}
	}
#else
#endif
}

inline bool IsProcessRunning(const FString& ProcessName) {

#if  !PLATFORM_LINUX
	bool IsRunning = false;

	const TCHAR* ProcessNameChar = *ProcessName;

	const HANDLE Snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (Snapshot != INVALID_HANDLE_VALUE) {
		PROCESSENTRY32 ProcessEntry;
		ProcessEntry.dwSize = sizeof(ProcessEntry);

		if (Process32First(Snapshot, &ProcessEntry)) {
			do {
				if (_wcsicmp(ProcessEntry.szExeFile, ProcessNameChar) == 0) {
					IsRunning = true;
					break;
				}
			} while (Process32Next(Snapshot, &ProcessEntry));
		}

		CloseHandle(Snapshot);
	}

	return IsRunning;
#else
	return true;
#endif
}

inline void LaunchURL(const FString& URL) {
	FPlatformProcess::LaunchURL(*URL, nullptr, nullptr);
}
