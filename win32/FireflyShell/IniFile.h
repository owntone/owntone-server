/*
 *(C) 2006 Roku LLC
 *
 * This program is free software; you can redistribute it and/or modify it 
 * under the terms of the GNU General Public License Version 2 as published 
 * by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but 
 * without any warranty; without even the implied warranty of 
 * merchantability or fitness for a particular purpose. See the GNU General 
 * Public License for more details.
 *
 * Please read README.txt in the same directory as this source file for 
 * further license information.
 */

#ifndef INIFILE_H
#define INIFILE_H
#include "DosPath.h"

class IniFile
{
	CString m_path;

public:
	explicit IniFile(const CString &path)
		: m_path(path)
	{
	}

	CString GetString(const TCHAR *section, const TCHAR *key, const TCHAR *defstring)
	{
		CString str;
		::GetPrivateProfileString(section, key, defstring, str.GetBuffer(512), 512, m_path);
		str.ReleaseBuffer();
		return str;
	}

	bool SetString(const TCHAR *section, const TCHAR *key, const TCHAR *value)
	{
		return ::WritePrivateProfileString(section, key, value, m_path) != 0;
	}

	int GetInteger(const TCHAR *section, const TCHAR *key, int defvalue)
	{
		return ::GetPrivateProfileInt(section, key, defvalue, m_path);
	}

	bool SetInteger(const TCHAR *section, const TCHAR *key, int value)
	{
		CString str;
		str.Format(_T("%d"), value);
		return ::WritePrivateProfileString(section, key, str, m_path) != 0;
	}

	bool IsWritable() const
	{
		if (::WritePrivateProfileString(_T("Writability Test"), _T("Writability Test"), _T("Test"), m_path))
		{
			// Remove it then.
			::WritePrivateProfileString(_T("Writability Test"), NULL, NULL, m_path);
			return true;
		}
		else
			return false;
	}
};
#endif // INIFILE_H
