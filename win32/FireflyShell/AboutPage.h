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

#ifndef ABOUTPAGE_H
#define ABOUTPAGE_H 1

#include "resource.h"

class CAboutPage :
	public CPropertyPageImpl<CAboutPage>,
	public CWinDataExchange<CAboutPage>
{
	typedef CPropertyPageImpl<CAboutPage> base;
	CListViewCtrl m_list;
	enum 
	{ 
		SUBITEM_DESCRIPTION = 0,
		SUBITEM_VERSION = 1,
		SUBITEM_PATH = 2,
		SUBITEM_COUNT = 3
	};

	int m_column_widths[SUBITEM_COUNT];
    
    CHyperLink m_roku_link;
    CHyperLink m_firefly_link;

	// String version of information ready to write to the clipboard
	CString m_versions;

public:
	CAboutPage()
	{
		::ZeroMemory(m_column_widths, sizeof(m_column_widths));
	}
	enum { IDD = IDD_PAGE_ABOUT };

private:

	// Message Handlers
	BEGIN_MSG_MAP(CAboutPage)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		COMMAND_ID_HANDLER(IDC_COPY, OnCopy)
		MSG_WM_CTLCOLORSTATIC(OnCtlColorStatic)
		CHAIN_MSG_MAP(base)
	END_MSG_MAP()

	BEGIN_DDX_MAP(CAboutPage)
		DDX_CONTROL_HANDLE(IDC_VERSIONLIST, m_list)
	END_DDX_MAP()

	LRESULT OnInitDialog(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
	LRESULT OnCopy(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnCtlColorStatic(HDC, HWND);

	void FillVersionList();
	void AddEntry(const TCHAR *path, const TCHAR *fallback_description);

	void AddItem(int item, int subitem, const TCHAR *text);
};

#endif // ABOUTPAGE_H