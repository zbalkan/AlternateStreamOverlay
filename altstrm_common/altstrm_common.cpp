#include "altstrm_common.h"

/*
 * 08/2011 lee@benf.org
 *
 * Originally based on the excellent article
 * http://www.flexhex.com/docs/articles/alternate-streams.phtml
 *
 * Updated to use functions now natively avaiable in vs2008.
 *
 * Helper utilities to extract a vector of (or test for presence of)
 * alternate streams.  
 *
 * I could check if the FS is NTFS, but that'd require caching to not be awful,
 * so I'd need to figure out removable drive notifications.... Manyana!
 *
 *
 */

#include <string>
#include <sstream>


class CSLock {
private:
	CRITICAL_SECTION & cs; 
public:
	CSLock(CRITICAL_SECTION & c) : cs(c) {
		EnterCriticalSection(&cs);
	}
	~CSLock() {
		LeaveCriticalSection(&cs);
	}
};


NTInnerXP::NTInnerXP() {
	OutputDebugStringA("Using NTQueryInformationFile, as we're < vista");
	InitializeCriticalSection(&csQuery);
	hNtDll = LoadLibrary(L"ntdll.dll");
	if (hNtDll == 0)
	{
		OutputDebugStringA("Unable to load NTDll.dll!");
		ntQueryInformationFile = 0;
	}
	else
	{
		ntQueryInformationFile = (tNtQueryInformationFile)GetProcAddress(hNtDll, "NtQueryInformationFile");
		if (!ntQueryInformationFile)
		{
			OutputDebugStringA("Unable to get NTQueryInformationFile");
		}
	}
}

NTInnerXP::~NTInnerXP() {
	OutputDebugStringA("Destroying altstrm NTDllFuncs");
	if (hNtDll != 0) FreeLibrary(hNtDll);
	DeleteCriticalSection(&csQuery);
	hNtDll = 0;
}


bool NTInnerXP::GetInformationByHandle(HANDLE hFile, PFILE_STREAM_INFO infoBlock, unsigned long length) {
	if (!ntQueryInformationFile) return false;
	IO_STATUS_BLOCK ioStatus;
	memset(infoBlock, 0, sizeof(infoBlock));
	{
		CSLock cslock(csQuery); // QueryInformationFile isn't guaranteed threadsafe IIUC.
		HRESULT hr = ntQueryInformationFile(hFile, &ioStatus, infoBlock, length, FileStreamInformation);
		if (!SUCCEEDED(hr)) {
			return false;
		}
	}
	return true;
}

NTInnerAPI::NTInnerAPI() {
	HMODULE kernelDll = GetModuleHandle(L"kernel32.dll");
	getFileInformationByHandleEx = (tGetFileInformationByHandleEx)GetProcAddress(kernelDll, "GetFileInformationByHandleEx");
	if (!getFileInformationByHandleEx)
	{
		OutputDebugStringA("Unable to get GetFileInformationByHandleEx");
	}
}

bool NTInnerAPI::GetInformationByHandle(HANDLE hFile, PFILE_STREAM_INFO infoBlock, unsigned long length) {
	if (!getFileInformationByHandleEx) return false;
	// Can I just use getFileInformationByHandleEx now??
	// Yes, if we can guarantee vista....
	if (FALSE == getFileInformationByHandleEx(hFile,FileStreamInfo, infoBlock, length)) {
		return false;
	}
	return true;
}

NTDllFuncs::NTDllFuncs() : inner(NULL)
{
	OutputDebugStringA("Initialising altstrm NTDllFuncs");
	// in vista and above, we can use a documented feature!
	OSVERSIONINFO vi;
	ZeroMemory(&vi, sizeof(vi));
	vi.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
	GetVersionEx(&vi);
	std::stringstream ss;
	bool bVistaOrHigher = vi.dwMajorVersion >= 6;

	ss << "Windows version " << vi.dwMajorVersion << "." << vi.dwMinorVersion << " Using API " << (bVistaOrHigher ? "Y" : "N");
	OutputDebugStringA(ss.str().c_str());

	if (bVistaOrHigher) {
		inner = new NTInnerAPI();
	} else {
		inner = new NTInnerXP();
	}
}

NTDllFuncs::~NTDllFuncs()
{
	delete inner;
	inner = 0;
}


bool NTDllFuncs::HasAlternateStreams(HANDLE hFile)
{
	// Enough space for all streams.
	BYTE infoBlock[64 * 1024];
	PFILE_STREAM_INFO pStreamInfo = (PFILE_STREAM_INFO)infoBlock;
	if (!inner->GetInformationByHandle(hFile, pStreamInfo, sizeof(infoBlock))) {
		return false;
	}
	
	/* 
	 * And we also need to check if this file is a directory.
	 */
	BY_HANDLE_FILE_INFORMATION fileInformation;
	if (!GetFileInformationByHandle(hFile, &fileInformation)) {
		return false;
	}
	bool isDirectory = 0 != (fileInformation.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
	//if (!bVistaOrHigher) {
	//	// can't do directories.
	//	if (isDirectory) return false;
	//}

	/*
	 * For a directory, we don't expect any StreamNameLength (0 name) normally, so if there is one,
	 * alternate streams are present.  
	 * (i.e. we don't require 2 names).
	 */
	if (pStreamInfo->StreamNameLength == 0) return false;
	if (pStreamInfo->NextEntryOffset == 0 && !isDirectory) return false;

	return true;
}

bool NTDllFuncs::HasAlternateStreams(LPCWSTR fname)
{
	HandleW hFile = ::CreateFile(fname, 0, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
	if (hFile == INVALID_HANDLE_VALUE) {
		return false; 
	}

	return HasAlternateStreams(hFile);
}

std::vector<std::wstring> NTDllFuncs::ListAlternateStreams(HANDLE hFile)
{
	// Enough space for all streams.
	BYTE infoBlock[64 * 1024];
	PFILE_STREAM_INFO pStreamInfo = (PFILE_STREAM_INFO)infoBlock;
	if (!inner->GetInformationByHandle(hFile, pStreamInfo, sizeof(infoBlock))) {
		return std::vector<std::wstring>();
	}

	WCHAR wszStreamName[MAX_PATH];
	int count = 0;
	std::vector<std::wstring> result;
	for (;;)
	{
		if (pStreamInfo->StreamNameLength == 0) break;

		++count;
		memcpy(wszStreamName, pStreamInfo->StreamName, pStreamInfo->StreamNameLength);
		wszStreamName[pStreamInfo->StreamNameLength / sizeof(WCHAR)] = L'\0';
		result.push_back(std::wstring(wszStreamName));

		if (pStreamInfo->NextEntryOffset == 0) break;
		pStreamInfo = (PFILE_STREAM_INFO)((LPBYTE)pStreamInfo + pStreamInfo->NextEntryOffset);
	}
	return result;
}

std::vector<std::wstring> NTDllFuncs::ListAlternateStreams(LPCWSTR fname)
{
	HandleW hFile = ::CreateFile(fname, 0, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
	if (hFile == INVALID_HANDLE_VALUE) 
	{
		return std::vector<std::wstring>();
	}

	std::vector<std::wstring> result = ListAlternateStreams(hFile);

	return result;
}
