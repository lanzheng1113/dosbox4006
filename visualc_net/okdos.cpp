#include <windows.h>
#include "okdos.h"

static BOOL CallCmd(LPCSTR szExeFile, LPCSTR szExeParam, BOOL bShow, DWORD dwWaitTime/* = INFINITE*/)
{
	if (szExeFile == NULL)
		return FALSE;
	if (strlen(szExeFile) < 3)
		return FALSE;

	int iShow;
	if (!bShow)
	{
		iShow = SW_HIDE;
	}
	else
	{
		iShow = SW_SHOW;
	}
	SHELLEXECUTEINFOA ShExecInfo = { 0 };
	ShExecInfo.cbSize = sizeof(SHELLEXECUTEINFOA);
	ShExecInfo.fMask = SEE_MASK_NOCLOSEPROCESS;
	ShExecInfo.hwnd = NULL;
	ShExecInfo.lpVerb = NULL;
	ShExecInfo.lpFile = szExeFile;
	ShExecInfo.lpParameters = szExeParam;
	ShExecInfo.lpDirectory = NULL;
	ShExecInfo.nShow = iShow;
	ShExecInfo.hInstApp = NULL;

	if (ShellExecuteExA(&ShExecInfo) && ShExecInfo.hProcess)
	{
		DWORD dwWait = WaitForSingleObject(ShExecInfo.hProcess, dwWaitTime);
		if (dwWait == WAIT_OBJECT_0)
		{
			DWORD dwExitCode = (DWORD)-1;
			if (GetExitCodeProcess(ShExecInfo.hProcess, &dwExitCode))
			{
				if (dwExitCode == 0)
					return TRUE;
			}
		}
		else if (dwWait == WAIT_TIMEOUT)
		{
			TerminateProcess(ShExecInfo.hProcess, WAIT_TIMEOUT);
		}
	}

	return FALSE;
}

bool okdos::do_extract( const std::string& t7zPath, const std::string& strima, const std::string& v_extract_to )
{
	std::string extract_to(v_extract_to);
	if (extract_to.empty())
	{
		CHAR szTemp[MAX_PATH] = { 0 };
		GetTempPathA(_countof(szTemp), szTemp);
		
		
		CHAR szTempFileName[MAX_PATH] = { 0 };
		srand(GetTickCount());
		for (int i=0; i!=8; i++)
		{
			szTempFileName[i] = 'A' + rand() % 26;
		}
		szTempFileName[9] = 0;
		if (szTemp[strlen(szTemp) -1] != '\\')
			strcat(szTemp, "\\");
		strcat(szTemp, szTempFileName);

		CreateDirectoryA(szTemp, NULL);
		extract_to = szTemp;
		m_need_del_folder = true;
	}

	m_t7zPath = t7zPath;
	m_ima = strima;
	m_extract_to = extract_to;

	std::string str7zParam = std::string("x \"") + strima + "\" -y -aos -o\"" + extract_to + "\"";
	if (!CallCmd(t7zPath.c_str(), str7zParam.c_str(), FALSE, 10000))
		return false;

	m_okdos_ready = true;
	return true;
}

okdos::okdos()
{
	m_okdos_ready = false;
	m_need_del_folder = false;
}

static BOOL DeleteDirectory(const char* szDirName)
{
	if (szDirName == NULL)
		return FALSE;

	char szDirBuf[MAX_PATH] = { 0 };
	strcpy_s(szDirBuf, szDirName);
	strcat_s(szDirBuf, "\\*");

	WIN32_FIND_DATAA wfd;
	HANDLE hFind = FindFirstFileA(szDirBuf, &wfd);
	if (hFind == INVALID_HANDLE_VALUE)
	{
		return FALSE;
	}
	do
	{
		if (strcmp(wfd.cFileName, ".") == 0 ||
			strcmp(wfd.cFileName, "..") == 0)
		{
			continue;
		}
		else
		{

			char szDirBuf[MAX_PATH] = { 0 };
			strcpy_s(szDirBuf, szDirName);
			strcat_s(szDirBuf, "\\");
			strcat_s(szDirBuf, wfd.cFileName);
			if (wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			{
				DeleteDirectory(szDirBuf);
			}
			else
			{
				//去掉只读属性
				SetFileAttributesA(szDirBuf, GetFileAttributesA(szDirBuf) & ~FILE_ATTRIBUTE_READONLY);
				DeleteFileA(szDirBuf);
				//printf("DeleteFileW: %ls\n", szDirBuf);
			}
		}
	} while (FindNextFileA(hFind, &wfd));
	FindClose(hFind);
	//
	//去掉只读属性，删除文件夹自身
	//
	SetFileAttributesA(szDirName, GetFileAttributesA(szDirName) & ~FILE_ATTRIBUTE_READONLY);
	//printf("RemoveDirectoryW: %ls\n", szDirName);
	if (!RemoveDirectoryA(szDirName))
	{
		//printf("Failed.\n");
		return FALSE;
	}
	return TRUE;
}

okdos::~okdos()
{
	if (m_need_del_folder && !m_extract_to.empty())
	{
		DeleteDirectory(m_extract_to.c_str());
	}
}

#include <sstream>
std::string okdos::make_bat_string()
{
	if (!m_okdos_ready)
	{
		return "";
	}
	std::stringstream ss;
	ss << "mount d " << m_extract_to << "\n"
		<< "mount c " << m_ima[0] << ":\\\n"
		<< "if not exist d:\\autoexec.bat exit\n"
		<< "d:\n"
		<< "autoexec.bat\n";
	return ss.str();
}