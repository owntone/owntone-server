/*
 * (C) 1997, 2006 Mike Crowe
 *
 * License: Do what you like with it except claim that you wrote it.
 */

#include "stdafx.h"
#include "dospath.h"
#include <stdlib.h>
#include <direct.h>

CDosPath::CDosPath(const TCHAR *pszPath, int nFlags)
{
	SetPath(pszPath, nFlags);
}

CDosPath::CDosPath(const CDosPath &old)
{
	m_drive = old.m_drive;
	m_dir = old.m_dir;
	m_file = old.m_file;
	m_ext = old.m_ext;
}

CDosPath &CDosPath::operator=(const CDosPath &old)
{
	m_drive = old.m_drive;
	m_dir = old.m_dir;
	m_file = old.m_file;
	m_ext = old.m_ext;
	return *this;
}
	
CDosPath::~CDosPath()
{
}

void CDosPath::SplitPath(const TCHAR *path)
{
#if USE_SECURE
	_tsplitpath_s(path, m_drive.GetBufferSetLength(_MAX_DRIVE), _MAX_DRIVE,
			     m_dir.GetBufferSetLength(_MAX_DIR), _MAX_DIR,
			     m_file.GetBufferSetLength(_MAX_FNAME), _MAX_FNAME,
			     m_ext.GetBufferSetLength(_MAX_EXT), _MAX_EXT);
#else
	_tsplitpath(path, m_drive.GetBufferSetLength(_MAX_DRIVE),
			   m_dir.GetBufferSetLength(_MAX_DIR),
			   m_file.GetBufferSetLength(_MAX_FNAME),
			   m_ext.GetBufferSetLength(_MAX_EXT));
#endif

	m_ext.ReleaseBuffer();
	m_file.ReleaseBuffer();
	m_dir.ReleaseBuffer();
	m_drive.ReleaseBuffer();
}

void CDosPath::SetPath(const TCHAR *pszPath, int nFlags)
{
	if (nFlags & PATH_ONLY)
	{
		CString temp(pszPath);
		temp += '\\';
		SplitPath(temp);
	}
	else
		SplitPath(pszPath);
}
	
CDosPath &CDosPath::operator|=(CDosPath &fullpath)
{
	if (m_drive.IsEmpty())
	{
//		TRACE1("Inserting drive %s\n", fullpath.m_szDrive);
		m_drive = fullpath.m_drive;
	}
	if (m_dir.IsEmpty())
	{
//		TRACE1("Inserting directory %s\n", fullpath.m_szDir);
		m_dir = fullpath.m_dir;
	}
	if (m_file.IsEmpty())
	{
//		TRACE1("Inserting file %s\n", fullpath.m_szFile);
		m_file = fullpath.m_file;
	}
	if (m_ext.IsEmpty())
	{
//		TRACE1("Inserting extension %s\n", fullpath.m_szExt);
		m_ext = fullpath.m_ext;
	}
	return *this;
}

CDosPath CDosPath::operator|(CDosPath &fullpath)
{
	CDosPath temp(GetPath());
	temp |= fullpath;
	return temp;
}

CString CDosPath::GetPath() const
{
	CString temp;
#if USE_SECURE
	_tmakepath_s(temp.GetBufferSetLength(_MAX_PATH), _MAX_PATH, m_drive, m_dir, m_file, m_ext);
#else
	_tmakepath(temp.GetBufferSetLength(_MAX_PATH), m_drive, m_dir, m_file, m_ext);
#endif
	temp.ReleaseBuffer();
	return temp;
}

CString CDosPath::GetPathOnly() const
{
	CString temp;
#if USE_SECURE
	_tmakepath_s(temp.GetBufferSetLength(_MAX_PATH), _MAX_PATH, m_drive, m_dir, NULL, NULL);
#else
	_tmakepath(temp.GetBufferSetLength(_MAX_PATH), m_drive, m_dir, NULL, NULL);
#endif
	temp.ReleaseBuffer();
	return temp;
}

CDosPath CDosPath::CurrentPath()
{
	TCHAR szBuffer[_MAX_PATH];
	_tgetcwd(szBuffer, _MAX_PATH);
	return CDosPath(szBuffer, PATH_ONLY);
}

CDosPath CDosPath::AppPath()
{
	TCHAR szBuffer[_MAX_PATH];
#ifdef _MFC
	GetModuleFileName(AfxGetApp()->m_hInstance, szBuffer, _MAX_PATH);
#else
	GetModuleFileName(GetModuleHandle(NULL), szBuffer, _MAX_PATH);
#endif
	return CDosPath(szBuffer);
}

CDosPath CDosPath::WindowsPath()
{
	TCHAR szBuffer[_MAX_PATH];
	GetWindowsDirectory(szBuffer, _MAX_PATH);
	return CDosPath(szBuffer, PATH_ONLY);
}

#ifdef MAKE_EXE
#include <stdio.h>

void main(int ac, char *av[])
{
	if (ac != 3)
	{
		printf("Usage: dospath incomplete complete\n");
		return;
	}
	CDosPath a = av[1];
	CDosPath b = av[2];
	a |= b;
	printf("%s |= %s = %s\n", av[1], av[2], (const char *) a.GetPath());
	printf("\n\n\n\n");
	a = CDosPath::CurrentPath();
	printf("Current = %s\n", (const char *)a.GetPath());
	a = CDosPath::AppPath();
	printf("App = %s\n", (const char *)a.GetPath());
	a = CDosPath::TempPath();
	printf("Temp = %s\n", (const char *)a.GetPath());
}
#endif
