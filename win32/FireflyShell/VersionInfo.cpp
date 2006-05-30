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

#include "stdafx.h"
#include "VersionInfo.h"

bool VersionInfo::Open(const TCHAR *filename)
{
	Close();

	DWORD useless;
	m_size = GetFileVersionInfoSize(filename, &useless);
	if (m_size)
	{
		m_buffer = operator new(m_size);
		::ZeroMemory(m_buffer, m_size);
		if (GetFileVersionInfo(filename, 0, static_cast<DWORD>(m_size), m_buffer))
		{
			return IdentifySubBlock();
		}
		else
			Close();
	}
	return false;
}

bool VersionInfo::IdentifySubBlock()
{
	m_subblock.Empty();
	WORD required_langid = LANGIDFROMLCID(GetUserDefaultLCID());

	UINT cbTranslate;
	struct LANGANDCODEPAGE 
	{
		WORD wLanguage;
		WORD wCodePage;
	} *lpTranslate;


	if (VerQueryValue(m_buffer, _T("\\VarFileInfo\\Translation"),
				      reinterpret_cast<LPVOID*>(&lpTranslate),
					  &cbTranslate))
	{
		// Try and find the user's language, but if we can't then just use the
		// first one in the file.
		int lang_index = -1;
		for(unsigned i=0; i < (cbTranslate/sizeof(struct LANGANDCODEPAGE)); i++ )
		{
			// If we have an exact match then great.
			if (lpTranslate[i].wLanguage == required_langid)
			{
				lang_index = i;
				break;
			}
			// Otherwise settle for a primary language match and keep looking
			else if ((PRIMARYLANGID(lpTranslate[i].wLanguage) == PRIMARYLANGID(required_langid)) && (lang_index < 0))
			{
				lang_index = i;
			}
		}
		if (lang_index < 0)
		{
			ATLTRACE("Failed to find a matching language. Just using the first one.\n");
			lang_index = 0;
		}

		m_subblock.Format(_T("\\StringFileInfo\\%04x%04x\\"), lpTranslate[lang_index].wLanguage, lpTranslate[lang_index].wCodePage);
		return true;
	}
	return false;
}

void VersionInfo::Close()
{
	if (m_buffer)
	{
		delete m_buffer;
		m_buffer = NULL;
	}
}

CString VersionInfo::GetString(const TCHAR *name) const
{
	CString path = m_subblock + name;

	LPVOID buffer;
	UINT cb;
	if (VerQueryValue(m_buffer, const_cast<LPTSTR>(static_cast<LPCTSTR>(path)), &buffer, &cb))
	{
		return CString(static_cast<LPCTSTR>(buffer), cb);
	}
	return CString();
}

//CString VersionInfo::GetStringFileVersion()
//{
//	VerQueryValue(m_buffer, 
//}