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

#ifndef MAINDLG_H
#define MAINDLG_H 1

#include "ConfigPage.h"
#include "AdvancedPage.h"
#include "LogPage.h"
#include "AboutPage.h"

class CMainDlg : public CPropertySheetImpl<CMainDlg>
{
	typedef CPropertySheetImpl<CMainDlg> base;

	CString m_strTitle;

	CConfigPage m_pageConfig;
	CAdvancedPage m_pageAdvanced;
	CLogPage m_pageLog;
	CAboutPage m_pageAbout;
	bool m_window_move_required;

	BEGIN_MSG_MAP(CMainDlg)
		MESSAGE_HANDLER(WM_COMMAND, OnCommand)
		MSG_WM_MOVE(OnMove)
		CHAIN_MSG_MAP(base)
	END_MSG_MAP()

	LRESULT OnCommand(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled);
	void OnMove(CPoint);
	void PositionWindow();

	void OnSheetInitialized();

public:
	// Pass true if you want the window to be moved to near the mouse pointer.
	// This is usually only the case if the window is displayed as a result of
	// clicking on the shell notification icon.
	CMainDlg(bool move_window);
};

#endif // MAINDLG_H