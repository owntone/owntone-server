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
#include "AdvancedPage.h"
#include "IniFile.h"
#include "FireflyShell.h"

LRESULT CAdvancedPage::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    CWaitCursor wait;

    IniFile ini(GetApplication()->GetConfigPath());
    m_server_port = ini.GetInteger(_T("general"), _T("port"), 9999);

    DoDataExchange(false);
    m_port_spin.SetRange32(1, 65535);

    if(GetApplication()->IsServiceAutoStartEnabled())
        m_autostart_check.SetCheck(BST_CHECKED);
    else
        m_autostart_check.SetCheck(BST_UNCHECKED);

    if(GetApplication()->IsAppletAutoStartEnabled())
        m_autostart_icon_check.SetCheck(BST_CHECKED);
    else
        m_autostart_icon_check.SetCheck(BST_UNCHECKED);

    UpdateControls();

    GetDlgItem(IDC_STARTSERVICE).SendMessage(BCM_SETSHIELD,0,TRUE);
    GetDlgItem(IDC_STOPSERVICE).SendMessage(BCM_SETSHIELD,0,TRUE);
    GetDlgItem(IDC_AUTOSTART).SendMessage(BCM_SETSHIELD,0,TRUE);

    GetApplication()->ServiceStatusSubscribe(this);
    return 0;
}

void CAdvancedPage::OnDestroy() {
    GetApplication()->ServiceStatusUnsubscribe(this);
}

void CAdvancedPage::UpdateControls() {
    Service::Status status = GetApplication()->GetServiceStatus();
    UpdateControls(status);
}

void CAdvancedPage::UpdateControls(Service::Status status)
{
    UINT state_id;
    if (status.IsPending())
    {
        state_id = IDS_SERVER_PENDING;
        GetDlgItem(IDC_STARTSERVICE).ShowWindow(SW_HIDE);
        GetDlgItem(IDC_STOPSERVICE).ShowWindow(SW_HIDE);
    }
    else if (status.IsRunning())
    {
        state_id = IDS_SERVER_RUNNING;
        GetDlgItem(IDC_STARTSERVICE).ShowWindow(SW_HIDE);
        GetDlgItem(IDC_STOPSERVICE).ShowWindow(SW_SHOW);
    }
    else
    {
        state_id = IDS_SERVER_STOPPED;
        GetDlgItem(IDC_STARTSERVICE).ShowWindow(SW_SHOW);
        GetDlgItem(IDC_STOPSERVICE).ShowWindow(SW_HIDE);
    }

    const bool can_configure = GetApplication()->CanConfigure();
    GetDlgItem(IDC_SERVERPORT).EnableWindow(can_configure);
    GetDlgItem(IDC_PORTSPIN).EnableWindow(can_configure);

    // If we can't control the service then don't give the user
    // the impression that we can.
    const bool can_control = GetApplication()->CanControlService();
    GetDlgItem(IDC_STARTSERVICE).EnableWindow(can_control);
    GetDlgItem(IDC_STOPSERVICE).EnableWindow(can_control);
    GetDlgItem(IDC_AUTOSTART).EnableWindow(can_control);

    CString state;
    state.LoadString(state_id);
    if (!can_control)
    {
        CString s;
        s.LoadString(IDS_NOT_ADMIN);
        state += " ";
        state += s;
    }

    GetDlgItem(IDC_SERVERSTATE).SetWindowText(state);
}

int CAdvancedPage::OnApply()
{
    CWaitCursor wait;

    ATLTRACE("CAdvancedPage::OnApply\n");

    if (!DoDataExchange(true))
        return false;

    IniFile ini(GetApplication()->GetConfigPath());
    ini.SetInteger(_T("general"), _T("port"), m_server_port);

    GetApplication()->EnableServiceAutoStart(m_hWnd,m_autostart_check.GetCheck() == BST_CHECKED);
    GetApplication()->EnableAppletAutoStart(m_hWnd,m_autostart_icon_check.GetCheck() == BST_CHECKED);

    // Incorrectly documented in WTL
    return true;
}

LRESULT CAdvancedPage::OnStartService(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
    GetApplication()->StartService(m_hWnd);
    UpdateControls();
    return 0;
}

LRESULT CAdvancedPage::OnStopService(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
    GetApplication()->StopService(m_hWnd);
    UpdateControls();
    return 0;
}

LRESULT CAdvancedPage::OnWebAdmin(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
    CWaitCursor wait;

    // Go to the config file because we might not have committed a change yet.
    IniFile ini(GetApplication()->GetConfigPath());
    unsigned int port = ini.GetInteger(_T("general"), _T("port"), 9999);

    CString url;
    url.Format(_T("http://localhost:%u/"), port);

    ::ShellExecute(m_hWnd, _T("open"), url, NULL, NULL, SW_SHOWNORMAL);
    return 0;
}

void CAdvancedPage::OnServiceStatus(Service::Status old_status, Service::Status new_status)
{
    UpdateControls(new_status);
}

