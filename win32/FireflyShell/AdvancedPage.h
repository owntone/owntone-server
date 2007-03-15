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

#ifndef ADVANCEDPAGE_H
#define ADVANCEDPAGE_H 1

#include "resource.h"
#include "ServiceControl.h"

/// @todo Users shouldn't be able to paste non-numbers into the port number.

class CAdvancedPage :
    public CPropertyPageImpl<CAdvancedPage>,
    public CWinDataExchange<CAdvancedPage>,
    public ServiceStatusObserver
{
public:
    enum { IDD = IDD_PAGE_ADVANCED };

private:
    typedef CPropertyPageImpl<CAdvancedPage> base;

    enum ServiceState
    {
        Pending = 0,
        Running = 1,
        Stopped = 2
    };

    enum { TIMER_ID = 42 };

    unsigned int m_server_port;
    CUpDownCtrl m_port_spin;
    CButton m_autostart_check;
    CButton m_autostart_icon_check;

    void UpdateControls(Service::Status status);
    void UpdateControls();

    // ServiceStatusObserver
    void OnServiceStatus(Service::Status old_status, Service::Status new_status);

    BEGIN_MSG_MAP(CAdvancedPage)
        MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
        COMMAND_HANDLER_EX(IDC_SERVERPORT, EN_CHANGE, OnChange)
        COMMAND_ID_HANDLER(IDC_STARTSERVICE, OnStartService)
        COMMAND_ID_HANDLER(IDC_STOPSERVICE, OnStopService)
        COMMAND_ID_HANDLER(IDC_WEBADMIN, OnWebAdmin)
        MSG_WM_DESTROY(OnDestroy)
        CHAIN_MSG_MAP(base)
    END_MSG_MAP()

    BEGIN_DDX_MAP(CAdvancedPage)
        DDX_UINT(IDC_SERVERPORT, m_server_port);
        DDX_CONTROL_HANDLE(IDC_PORTSPIN, m_port_spin);
        DDX_CONTROL_HANDLE(IDC_AUTOSTART, m_autostart_check);
        DDX_CONTROL_HANDLE(IDC_AUTOSTART_ICON, m_autostart_icon_check);
    END_DDX_MAP()

    // MessageHandlers;
    LRESULT OnInitDialog(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnStartService(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
    LRESULT OnStopService(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
    LRESULT OnWebAdmin(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
    void OnDestroy();
    void OnTimer(UINT id, TIMERPROC proc);
    int OnApply();
    void OnChange(UINT uCode, int nCtrlID, HWND hwndCtrl) {
        // Lots of things could have changed.
        SetModified();
    }
};

#endif // ADVANCEDPAGE_H