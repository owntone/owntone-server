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

#ifndef LOGPAGE_H
#define LOGPAGE_H 1

#include "resource.h"

class CLogPage :
	public CPropertyPageImpl<CLogPage>
{
	typedef CPropertyPageImpl<CLogPage> base;

public:
	enum { IDD = IDD_PAGE_LOG };

private:

	// Message Handlers
	BEGIN_MSG_MAP(CLogPage)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		COMMAND_HANDLER(IDC_REFRESH, BN_CLICKED, OnRefresh)
		CHAIN_MSG_MAP(base)
	END_MSG_MAP()

	LRESULT OnInitDialog(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
	LRESULT OnRefresh(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);

	void LoadLog();

};

#endif // LOGPAGE_H