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
#include "AboutPage.h"
#include "FireflyShell.h"
#include "VersionInfo.h"
#include "DosPath.h"

LRESULT CAboutPage::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    m_firefly_link.SubclassWindow(GetDlgItem(IDC_FIREFLYLINK));
    m_firefly_link.SetHyperLink(_T("http://forums.fireflymediaserver.org"));
    
    m_roku_link.SetHyperLink(_T("http://www.rokulabs.com"));
    m_roku_link.SubclassWindow(GetDlgItem(IDC_ROKULINK));


    // Do this before we try and use the controls.
	DoDataExchange(false);

	FillVersionList();

	return 0;
}

void CAboutPage::FillVersionList()
{
	CWaitCursor wait;

	m_versions.Empty();

	// Initialise the list control
	CString str;
	str.LoadString(IDS_VERSIONINFO_DESCRIPTION);
	m_list.AddColumn(str, SUBITEM_DESCRIPTION);
	str.LoadString(IDS_VERSIONINFO_VERSION);
	m_list.AddColumn(str, SUBITEM_VERSION, 1);
	str.LoadString(IDS_VERSIONINFO_PATH);
	m_list.AddColumn(str, SUBITEM_PATH, 2);

	m_list.SetColumnWidth(SUBITEM_DESCRIPTION, 40);
	m_list.SetColumnWidth(SUBITEM_VERSION, 40);

	CDosPath server_path(GetApplication()->GetServiceBinaryPath());
	AddEntry(server_path.GetPath(), _T("Firefly server"));
	AddEntry(CDosPath::AppPath().GetPath(), _T("FireflyShell"));

	CString plugins_path = server_path.GetPathOnly() + _T("plugins\\");
	CString plugins_pattern = plugins_path + _T("*.dll");
	WIN32_FIND_DATA find;
	HANDLE hFind = FindFirstFile(plugins_pattern, &find);
	if (hFind != INVALID_HANDLE_VALUE)
	{
		do
		{
			AddEntry(plugins_path + find.cFileName, CString(find.cFileName) + _T(" plugin"));
		} while (FindNextFile(hFind, &find));
		FindClose(hFind);
	}

	for(int i = 0; i < SUBITEM_COUNT; ++i)
	{
		m_list.SetColumnWidth(i, m_column_widths[i] + 16);
	}
}

void CAboutPage::AddEntry(const TCHAR *path, const TCHAR *fallback_description)
{
	VersionInfo vi;

	CString description = fallback_description;
	CString version;

	if (vi.Open(path))
	{
		description = vi.GetFileDescription();
		version = vi.GetFileVersion();
	}

	int item = m_list.GetItemCount();
	AddItem(item, SUBITEM_DESCRIPTION, description);
	AddItem(item, SUBITEM_VERSION, version);
	AddItem(item, SUBITEM_PATH, path);

	CString line;
	line.Format(_T("%s\t%s\t%s\r\n"), description, version, path);
	m_versions += line;
}

void CAboutPage::AddItem(int item, int subitem, const TCHAR *text)
{
	m_list.AddItem(item, subitem, text);
	const int width = m_list.GetStringWidth(text);
	if (width > m_column_widths[subitem])
		m_column_widths[subitem] = width;
}

LRESULT CAboutPage::OnCopy(WORD, WORD, HWND, BOOL &)
{
	if (OpenClipboard())
	{
		const size_t len = m_versions.GetLength() * sizeof(TCHAR);

		HGLOBAL h = ::GlobalAlloc(GMEM_MOVEABLE, len);
		if (h)
		{
			void *buffer = ::GlobalLock(h);
			memcpy(buffer, static_cast<const TCHAR *>(m_versions), len);
			::GlobalUnlock(h);

			EmptyClipboard();
#if defined(UNICODE)
			SetClipboardData(CF_UNICODETEXT, h);
#else
			SetClipboardData(CF_TEXT, h);
#endif
		}
		CloseClipboard();
	}
	return 0;
}

LRESULT CAboutPage::OnCtlColorStatic(HDC hdc, HWND hwnd)
{
	if (GetDlgItem(IDC_LOGO) == hwnd)
	{
		HBRUSH brush = (HBRUSH)::GetStockObject(WHITE_BRUSH);
		return (LRESULT)brush;
	}
	else
		return 0;
}