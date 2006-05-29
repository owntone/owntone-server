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

#ifndef CONFIGPAGE_H
#define CONFIGPAGE_H 1

#include "resource.h"

class CMainDlg;

class CConfigPage :
	public CPropertyPageImpl<CConfigPage>,
	public CWinDataExchange<CConfigPage>
{
	typedef CPropertyPageImpl<CConfigPage> base;

public:
	enum { IDD = IDD_PAGE_BASIC };

private:
	CString m_media_path;
	CString m_server_name;
	CString m_password;
	CButton m_protect_checkbox;

	void EnableControls();

	BEGIN_MSG_MAP(thisClass)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		COMMAND_ID_HANDLER(IDC_BROWSE, OnBrowse)
		COMMAND_HANDLER_EX(IDC_PROTECT, BN_CLICKED, OnClickProtect)
		COMMAND_HANDLER_EX(IDC_PASSWORD, EN_CHANGE, OnChange)
		COMMAND_HANDLER_EX(IDC_SERVERNAME, EN_CHANGE, OnChange)
		COMMAND_HANDLER_EX(IDC_PATH, EN_CHANGE, OnChange)
		CHAIN_MSG_MAP(base)
	END_MSG_MAP()

	BEGIN_DDX_MAP(CConfigPage)
		DDX_TEXT(IDC_PATH, m_media_path);
		DDX_TEXT(IDC_SERVERNAME, m_server_name);
		DDX_TEXT(IDC_PASSWORD, m_password);
		DDX_CONTROL_HANDLE(IDC_PROTECT, m_protect_checkbox);
	END_DDX_MAP()

	// Message handlers
	LRESULT OnInitDialog(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
	LRESULT OnBrowse(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	void OnClickProtect(UINT uCode, int nCtrlID, HWND hwndCtrl);
	int OnApply();
	void OnChange(UINT uCode, int nCtrlID, HWND hwndCtrl)
	{
		// Lots of things could have changed.
		SetModified();
	}
};

#endif // CONFIGPAGE_H