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

CMainDlg::CMainDlg(bool move_window)
	: m_window_move_required(move_window)
{
	this->AddPage(m_pageConfig);
	this->AddPage(m_pageAdvanced);
	this->AddPage(m_pageLog);
	this->AddPage(m_pageAbout);

	ATLVERIFY(m_strTitle.LoadString(IDR_MAINFRAME));

	this->SetTitle(m_strTitle);
}

void CMainDlg::OnSheetInitialized()
{
   HICON hIcon = (HICON)::LoadImage(_Module.GetResourceInstance(), MAKEINTRESOURCE(IDR_MAINFRAME),
           IMAGE_ICON, ::GetSystemMetrics(SM_CXICON), ::GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR);
   SetIcon(hIcon, TRUE);
   HICON hIconSmall = (HICON)::LoadImage(_Module.GetResourceInstance(), MAKEINTRESOURCE(IDR_MAINFRAME),
           IMAGE_ICON, ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);
   SetIcon(hIconSmall, FALSE);
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

void CMainDlg::PositionWindow()
{
	POINT cursor_pt;
	GetCursorPos(&cursor_pt);

	CRect desktop_rect;
	if (SystemParametersInfo(SPI_GETWORKAREA, 0, &desktop_rect, 0))
	{
		// Don't put it hard against the edge of the screen or task bar.
		desktop_rect.DeflateRect(CSize(4, 4));

		CRect window_rect;
		GetWindowRect(&window_rect);

		// First move the window so that it's middle is at the cursor position.
		CPoint pos;

		pos.x = cursor_pt.x - window_rect.Width()/2;
		pos.y = cursor_pt.y - window_rect.Height()/2;

		// Now make that window appear on the work area but in case it doesn't fit prefer to fit the
		// top left.
		if (pos.x + window_rect.Width() > desktop_rect.right)
			pos.x = desktop_rect.right - window_rect.Width();
		if (pos.y + window_rect.Height() > desktop_rect.bottom)
			pos.y = desktop_rect.bottom - window_rect.Height();

		if (pos.x < desktop_rect.left)
			pos.x = desktop_rect.left;
		if (pos.y < desktop_rect.top)
			pos.y = desktop_rect.top;

		SetWindowPos(NULL, pos.x, pos.y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOZORDER);
	}
}

void CMainDlg::OnMove(CPoint pt)
{
	if (m_window_move_required)
	{
		// We don't want to recurse.
		m_window_move_required = false;
		PositionWindow();
	}
	else
	{
		SetMsgHandled(false);
	}
}