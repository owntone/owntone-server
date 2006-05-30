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
#include "resource.h"

#include "MainDlg.h"
#include "FireflyShell.h"

CMainDlg::CMainDlg()
{
	this->AddPage(m_pageConfig);
	this->AddPage(m_pageAdvanced);
	this->AddPage(m_pageLog);
	this->AddPage(m_pageAbout);

	ATLVERIFY(m_strTitle.LoadString(IDR_MAINFRAME));
	this->SetTitle(m_strTitle);
}

LRESULT CMainDlg::OnCommand(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
{
	bool restart_service = false;
	if (HIWORD(wParam) == BN_CLICKED)
	{
		const UINT id = LOWORD(wParam);

		if ((id == IDOK || id == ID_APPLY_NOW) 
			&& GetDlgItem(ID_APPLY_NOW).IsWindowEnabled()
			&& GetApplication()->GetServiceStatus().IsRunning())
		{
			CString title, text;
			title.LoadString(IDR_MAINFRAME);
			text.LoadString(IDS_QUERYSERVERRESTART);
			if (MessageBox(text, title, MB_YESNO) != IDYES)
				return 0;
			restart_service = true;
		}
	}		

	LRESULT result = base::OnCommand(uMsg, wParam, lParam, bHandled);
	if (restart_service)
		GetApplication()->RestartService(m_hWnd);

	return result;
}
