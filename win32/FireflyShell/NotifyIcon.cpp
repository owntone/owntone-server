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
#include "NotifyIcon.h"
#include "FireflyShell.h"

CNotifyMsg::CNotifyMsg(UINT32 id, UINT32 intval, const CString &strval) {
    this->id = id;
    this->intval = intval;
    this->strval = strval;
}


CNotifyIcon::CNotifyIcon()
{
	ZeroMemory(&m_nid, sizeof(NOTIFYICONDATA));
	m_nid.cbSize = sizeof(NOTIFYICONDATA);
	m_nid.uID = ID_SHELLNOTIFY;

	m_running_icon = AtlLoadIcon(IDI_SHELL_RUNNING);
	m_stopped_icon = AtlLoadIcon(IDI_SHELL_STOPPED);
}

BOOL CNotifyIcon::Create()
{
	m_registered_activation_message = GetApplication()->GetRegisteredActivationMessage();

	RECT rect = {0, 0, 0, 0};
	
	// Hidden window.
	if (base::Create(NULL, rect, _T("FireflyShellNotifyIconHidden"), WS_POPUP))
	{
		m_nid.uFlags = NIF_ICON|NIF_MESSAGE|NIF_TIP;

		InflictIconState();

		//wcsncpy(niData.szTip, TEXT("Foo?"), sizeof(niData.szTip));
		m_nid.hWnd = m_hWnd;
		m_nid.uCallbackMessage = PRIVATE_WM_NOTIFYICON;

		Shell_NotifyIcon(NIM_ADD, &m_nid);

		SetTimer(TIMER_ID, 5000, NULL);

		GetApplication()->ServiceStatusSubscribe(this);

		EnableUserSwitchNotifications();

		return TRUE;
	}

	return FALSE;
}

void CNotifyIcon::Destroy()
{
	GetApplication()->ServiceStatusUnsubscribe(this);
	KillTimer(TIMER_ID);
	Shell_NotifyIcon(NIM_DELETE, &m_nid);
	DestroyIcon(m_nid.hIcon);


	base::DestroyWindow();
}

void CNotifyIcon::OnClose()
{
	// The only time this should happen is if something else explicitly
	// sends us the message (such as the installer). We'll just
	// exit completely.
	GetApplication()->Exit();
}

void CNotifyIcon::PopupBalloon(CString &title, CString &text, DWORD flags) {
    m_nid.uFlags |= NIF_INFO;
    SafeStringCopy(m_nid.szInfoTitle, title, 64);
    SafeStringCopy(m_nid.szInfo,text, 256);
    m_nid.dwInfoFlags = flags;
    m_nid.uTimeout = 10000;
    Shell_NotifyIcon(NIM_MODIFY, &m_nid);
}

void CNotifyIcon::PopupBalloon(UINT title_id, UINT text_id, DWORD flags) {
    CString title, text;
    title.LoadString(title_id);
    text.LoadString(text_id);

    PopupBalloon(title, text, flags);
}

void CNotifyIcon::Update()
{
	InflictIconState();
	// I suspect we'll need this line too.
	// m_nid.uFlags &= ~NIF_INFO;
	Shell_NotifyIcon(NIM_MODIFY, &m_nid);
}

void CNotifyIcon::InflictIconState()
{
	// Will the icons leak?
	Service::Status status = GetApplication()->GetServiceStatus();
	UINT state_id;
	if (status.IsPending())
	{
		state_id = IDS_SERVER_PENDING;
		m_nid.hIcon = m_stopped_icon; // As good as any?
	}
	else if (status.IsRunning())
	{
		state_id = IDS_SERVER_RUNNING;
		m_nid.hIcon = m_running_icon;
	}
	else
	{
		state_id = IDS_SERVER_STOPPED;
		m_nid.hIcon = m_stopped_icon;
	}

	CString tip;
	tip.LoadString(state_id);
	SafeStringCopy(m_nid.szTip, tip, 64);
}

void CNotifyIcon::OnServiceStatus(Service::Status old_status, Service::Status new_status)
{
	Update();
}

void CNotifyIcon::OnTimer(UINT id, TIMERPROC proc)
{
	if (id == TIMER_ID)
	{
		GetApplication()->CheckServiceStatus();
	}
}

LRESULT CNotifyIcon::OnNotifyIconMessage(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
	switch (lParam)
	{
	case WM_LBUTTONDBLCLK:
		GetApplication()->Configure(true);
		bHandled = true;
		return 0L;
	case WM_RBUTTONDOWN:
	case WM_CONTEXTMENU:
		OnContextMenu();
		bHandled = true;
		return 0L;
	}

	return 0L;
}

void CNotifyIcon::OnContextMenu()
{
	HMENU hMenu;

	HINSTANCE hInstance = _Module.GetResourceInstance();
	hMenu = ::LoadMenu(hInstance, MAKEINTRESOURCE(IDM_CONTEXT));

	POINT pt;
	GetCursorPos(&pt);

	// See TrackPopupMenu in MSDN.
	SetForegroundWindow(m_hWnd);
	//::SetForegroundWindow(m_hWnd);
	HMENU hPopup = GetSubMenu(hMenu, 0);
	::SetMenuDefaultItem(hPopup, ID_CONFIGURE, FALSE);
	TrackPopupMenu(hPopup, TPM_LEFTALIGN | TPM_BOTTOMALIGN, pt.x, pt.y, 0, m_hWnd, NULL);
	::PostMessage(m_hWnd, WM_NULL, 0, 0);

	::DestroyMenu(hMenu);
}

LRESULT CNotifyIcon::OnConfigure(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
	GetApplication()->Configure(true);
	return 0;
}

LRESULT CNotifyIcon::OnExit(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
	/// @todo
	Application::GetInstance()->Exit();
	return 0;
}

LRESULT CNotifyIcon::OnRegisteredActivation(UINT, WPARAM, LPARAM, BOOL &bHandled)
{
	ATLTRACE(_T("Activate\n"));
	bHandled = true;
	GetApplication()->Configure(false);

	// We return a magic number so that the caller knows we've been found
	// and can give up.
	return m_registered_activation_message;
}

void CNotifyIcon::OnServerEvent(UINT32 id, UINT32 intval, const CString &strval) {
    // Note that we're running on a different thread here. We need to punt
    // the event over to the main thread using SendMessage.
    CNotifyMsg *pMsgNew = new CNotifyMsg(id, intval,strval);

    switch (id) {
    case 0:
    case 1:
    case 2:
        SendMessage(WM_SERVEREVENT, id, (LPARAM) pMsgNew);
        break;
    default:
        ATLASSERT(false);
        break;
    }
}

LRESULT CNotifyIcon::OnServerEvent(UINT, WPARAM wparam, LPARAM lparam, BOOL &bHandled) {
    CNotifyMsg *pMsgNotify = (CNotifyMsg *)lparam;
    bHandled = true;
    CString title;
    
    title.LoadString(IDR_MAINFRAME);

//    CString title, text;
//    title.LoadString(title_id);

    switch (wparam) {
        case 0:
            if(pMsgNotify->GetIntval() == 0)
                PopupBalloon(title,pMsgNotify->GetStrval());
            break;
        case 1:
            PopupBalloon(IDR_MAINFRAME, IDS_SCAN_START);
            break;
        case 2:
            PopupBalloon(IDR_MAINFRAME, IDS_SCAN_STOP);
            break;
    }
    delete(pMsgNotify);
    return 0;
}

void CNotifyIcon::EnableUserSwitchNotifications()
{
	HMODULE h = ::LoadLibrary(_T("WtsApi32.dll"));
	if (h)
	{
		typedef BOOL  (WINAPI *Proc)(HWND, DWORD);
		Proc fn = reinterpret_cast<Proc>(GetProcAddress(h, "WTSRegisterSessionNotification"));
		if (fn)
		{
			(*fn)(m_hWnd, NOTIFY_FOR_THIS_SESSION);
		}
		::FreeLibrary(h);
	}
}

LRESULT CNotifyIcon::OnSessionChange(UINT, WPARAM wparam, LPARAM, BOOL &bHandled)
{
	// Because only one process can get events through the mailslot we
	// disconnect from it when the user uses XP fast user switching to
	// switch to a different user.
	switch (wparam)
	{
	case WTS_CONSOLE_CONNECT:
	case WTS_REMOTE_CONNECT:
		ATLTRACE("SESSION CONNECT\n");
		GetApplication()->EnableServerEvents(true);
		break;

	case WTS_CONSOLE_DISCONNECT:
	case WTS_REMOTE_DISCONNECT:
		ATLTRACE("SESSION DISCONNECT\n");
		GetApplication()->EnableServerEvents(false);
		break;
	}
	bHandled = true;
	return 0;
}

