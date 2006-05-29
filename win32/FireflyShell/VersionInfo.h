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

#ifndef VERSIONINFO_H
#define VERSIONINFO_H

class VersionInfo
{
	size_t m_size;
	void *m_buffer;
	CString m_subblock;

	bool IdentifySubBlock();

	CString GetString(const TCHAR *name) const;

public:
	VersionInfo() : m_size(0), m_buffer(NULL) {}

	bool Open(const TCHAR *filename);
	void Close();

	CString GetFileDescription() const
	{
		return GetString(_T("FileDescription"));
	}

	CString GetFileVersion() const
	{
		return GetString(_T("FileVersion"));
	}
};
#endif // VERSIONINFO_H
