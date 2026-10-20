#ifndef _ALTSTRM_COMMON_H_
#define _ALTSTRM_COMMON_H_ 1

#include <shlobj.h> 
#include <comdef.h> 
#include <vector> 
#include <string> 
#include <windows.h> 


class HandleW
{
	HANDLE m_hnd;
public:
	HandleW(HANDLE hnd)
	{
		m_hnd = hnd;
	}
	~HandleW()
	{
		if (m_hnd != INVALID_HANDLE_VALUE) close();
	}
	void close()
	{
		::CloseHandle(m_hnd);
	}
	operator HANDLE() 
	{
		return m_hnd;
	}
};

/*
 * Avoid requiring the DDK headers.
 */
typedef struct IO_STATUS_BLOCK
{
	union
	{
		NTSTATUS Status;
		PVOID Pointer;
	};
	ULONG_PTR Information;
} IO_STATUS_BLOCK, *PIO_STATUS_BLOCK;

typedef enum FILE_INFORMATION_CLASS
{
	FileStreamInformation = 22,
};


class INTInner {
public:
	virtual bool GetInformationByHandle(HANDLE FileHandle, PFILE_STREAM_INFO infoBlock, unsigned long length) = 0;
};

// In XP we have to use this undocumented function.
typedef NTSTATUS (WINAPI *tNtQueryInformationFile)(HANDLE FileHandle, IO_STATUS_BLOCK * pIOStatusBlock, 
									   void * pOutFileInformation, unsigned long length, 
									   FILE_INFORMATION_CLASS fileInformationClass);



// In vista and higher, we can get away with this instead.

typedef BOOL (WINAPI *tGetFileInformationByHandleEx)(
    __in  HANDLE hFile,
    __in  FILE_INFO_BY_HANDLE_CLASS FileInformationClass,
    __out_bcount(dwBufferSize) LPVOID lpFileInformation,
    __in  DWORD dwBufferSize
);

class NTInnerXP : public INTInner {
	CRITICAL_SECTION csQuery;
	HMODULE hNtDll;
	tNtQueryInformationFile ntQueryInformationFile;
public:
	NTInnerXP();
	~NTInnerXP();
public:
	virtual bool GetInformationByHandle(HANDLE FileHandle, PFILE_STREAM_INFO infoBlock, unsigned long length);
};

class NTInnerAPI : public INTInner {
	tGetFileInformationByHandleEx getFileInformationByHandleEx;
public:
	NTInnerAPI();
	virtual bool GetInformationByHandle(HANDLE FileHandle, PFILE_STREAM_INFO infoBlock, unsigned long length);
};

class NTDllFuncs
{
private:
	INTInner * inner;
public:
	NTDllFuncs();
	~NTDllFuncs();

	bool HasAlternateStreams(HANDLE hFile);
	bool HasAlternateStreams(LPCWSTR fname);
	std::vector<std::wstring> ListAlternateStreams(HANDLE hFile);
	std::vector<std::wstring> ListAlternateStreams(LPCWSTR fname);
};

#endif // include guard


