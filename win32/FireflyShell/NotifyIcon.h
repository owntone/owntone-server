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

#ifndef NOTIFYICON_H
#define NOTIFYICON_H

#include "ServiceControl.h"
#include "ServerEvents.h"

class CNotifyMsg {
    UINT32 id;
    UINT32 intval;
    CString strval;

public:
    CNotifyMsg(UINT32 id, UINT32 intval, const CString &strval);
    CString &GetStrval(void) { return strval; };
    UINT32 GetIntval(void) { return intval; };
};


class CNotifyIcon 
	: public CWindowImpl<CNotifyIcon>,
	  public ServiceStatusObserver,
	  public ServerEvents::Observer
{
	typedef CWindowImpl<CNotifyIcon> base;

	NOTIFYICONDATA m_nid;
	HICON m_running_icon;
	HICON m_stopped_icon;
	UINT m_registered_activation_message;

	enum { TIMER_ID = 43 };
	enum { WM_SERVEREVENT = WM_APP + 42 };

	enum { PRIVATE_WM_NOTIFYICON = WM_USER + 42 };

	BEGIN_MSG_MAP(CNotifyIcon)
		MESSAGE_HANDLER(PRIVATE_WM_NOTIFYICON, OnNotifyIconMessage)
		COMMAND_ID_HANDLER(ID_CONFIGURE, OnConfigure)
		COMMAND_ID_HANDLER(ID_EXIT, OnExit)
		MESSAGE_HANDLER(m_registered_activation_message, OnRegisteredActivation)
		MESSAGE_HANDLER(WM_SERVEREVENT, OnServerEvent)
		MESSAGE_HANDLER(WM_WTSSESSION_CHANGE, OnSessionChange)
		MSG_WM_TIMER(OnTimer)
		MSG_WM_CLOSE(OnClose)
	END_MSG_MAP()

	// Message handlers
	void OnContextMenu();
    LRESULT OnNotifyIconMessage(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
	LRESULT OnConfigure(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnExit(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnRegisteredActivation(UINT, WPARAM, LPARAM, BOOL &bHandled);
	LRESULT OnServerEvent(UINT, WPARAM, LPARAM, BOOL &bHandled);
	LRESULT OnSessionChange(UINT, WPARAM, LPARAM, BOOL &bHandled);
	void OnTimer(UINT id, TIMERPROC proc);
	void OnClose();

	void PopupBalloon(UINT title_id, UINT text_id, DWORD flags = NIIF_INFO);
        void PopupBalloon(CString &title, CString &text, DWORD flags = NIIF_INFO);

	void InflictIconState();
	void Update();

	// Terminal services stuff on XP.
	void EnableUserSwitchNotifications();

	// ServiceStatusObserver
	void OnServiceStatus(Service::Status old_status, Service::Status new_status);

	// ServerEvents::Observer
	void OnServerEvent(UINT32 id, UINT32 intval, const CString &str);

public:
	CNotifyIcon();
	BOOL Create();
	void Destroy();

};

#endif // NOTIFYICON_H