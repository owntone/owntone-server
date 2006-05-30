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
#include "ConfigPage.h"
#include "FireflyShell.h"
#include "IniFile.h"

LRESULT CConfigPage::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
	CWaitCursor wait;

	IniFile ini(GetApplication()->GetConfigPath());
	
	m_server_name = ini.GetString(_T("general"), _T("servername"), _T("Firefly media server"));
	m_media_path = ini.GetString(_T("general"), _T("mp3_dir"), _T("C:\\Music"));
	m_password = ini.GetString(_T("general"), _T("password"), _T(""));

	// Do this before we try and use the controls.
	DoDataExchange(false);

	const bool enable_password = !m_password.IsEmpty();
	m_protect_checkbox.SetCheck(enable_password);
	EnableControls();
	return 0;
}

void CConfigPage::EnableControls()
{
	const bool enable = GetApplication()->CanConfigure();
	GetDlgItem(IDC_SERVERNAME).EnableWindow(enable);
	GetDlgItem(IDC_PATH).EnableWindow(enable);
	GetDlgItem(IDC_PROTECT).EnableWindow(enable);
	GetDlgItem(IDC_BROWSE).EnableWindow(enable);

	const bool enable_password = (m_protect_checkbox.GetCheck() != 0) && enable;
	GetDlgItem(IDC_PASSWORD).EnableWindow(enable_password);
	//GetDlgItem(IDC_PASSWORD_PROMPT).EnableWindow(enable_password);
}

int CConfigPage::OnApply()
{
	CWaitCursor wait;

	ATLTRACE("CConfigPage::OnApply\n");
	if (!DoDataExchange(true))
		return false;

	IniFile ini(GetApplication()->GetConfigPath());
	ini.SetString(_T("general"), _T("servername"), m_server_name);
	ini.SetString(_T("general"), _T("mp3_dir"), m_media_path);
	ini.SetString(_T("general"), _T("password"), m_password);

	// Incorrectly documented in WTL
	return true;
}

LRESULT CConfigPage::OnBrowse(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
	CFolderDialog folder;

	// Who cares if it fails.
	DoDataExchange(true);

	folder.SetInitialFolder(m_media_path);

	if (folder.DoModal() == IDOK)
	{
		m_media_path = folder.GetFolderPath();
		DoDataExchange(false);
	}

	return 0;
}

void CConfigPage::OnClickProtect(UINT uCode, int nCtrlID, HWND hwndCtrl)
{
	EnableControls();
	SetModified();
}