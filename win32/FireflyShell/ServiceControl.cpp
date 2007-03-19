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
#include "ServiceControl.h"
#include "DosPath.h"

bool Service::ExecHelper(const TCHAR *szAction) {
    SHELLEXECUTEINFO si;
    ZeroMemory(&si,sizeof(si));

    si.cbSize = sizeof(si);
    si.fMask = SEE_MASK_FLAG_NO_UI | SEE_MASK_NOCLOSEPROCESS;
    si.hwnd = NULL;
    si.lpVerb = _T("open");

    CDosPath path = CDosPath::AppPath();
    CString strPath = path.GetPathOnly();
    si.lpDirectory = static_cast<LPCTSTR>(strPath);

    CDosPath filename = CDosPath(_T("svcctrl.exe"));
    filename |= path;
    CString strFilename = filename.GetPath();

    si.lpFile = static_cast<LPCTSTR>(strFilename);

    CString strParams;
    strParams.Format(_T("%s \"%s\""),szAction,m_name);
    si.lpParameters = static_cast<LPCTSTR>(strParams);

    si.nShow = 0;

    if(!ShellExecuteEx(&si)) 
        return false;

    WaitForSingleObject(si.hProcess, INFINITE);

    DWORD dwResult;
    GetExitCodeProcess(si.hProcess,&dwResult);

    if(dwResult)
        return false;

    return true;
}

bool Service::Open(const TCHAR *name)
{
    Close();

    const DWORD USER_ACCESS = SC_MANAGER_CONNECT | SC_MANAGER_ENUMERATE_SERVICE | STANDARD_RIGHTS_READ;

    DWORD dwDesiredAccess = USER_ACCESS;

    m_sc_manager = ::OpenSCManager(NULL, SERVICES_ACTIVE_DATABASE, dwDesiredAccess);
    
    if (!m_sc_manager)
        m_can_control = false;
    else
        m_can_control = true;

    if (m_sc_manager) {
        m_sc_service = ::OpenService(m_sc_manager, name, dwDesiredAccess);
        if (m_sc_service) {
            m_name = name;
            return true;
        }
    }

    Close();
    return false;
}

void Service::Close() {
    if (m_sc_service) {
        ::CloseServiceHandle(m_sc_service);
        m_sc_service = NULL;
    }
    if (m_sc_manager) {
        ::CloseServiceHandle(m_sc_manager);
        m_sc_manager = NULL;
    }
}

bool Service::GetStatus(Status *status) const
{
    if (::QueryServiceStatus(m_sc_service, status))
        return true;
    else
        return false;
}

bool Service::Start()
{
    if (ExecHelper(_T("start")))
        return true;
    else
        return false;
}

bool Service::StartAndWait()
{
    if (Start())
    {
        Status status;
        return WaitPending(&status, SERVICE_START_PENDING);
    }
    else
        return false;
}

bool Service::Stop()
{
    Status status;
    if (ExecHelper(_T("stop")))
        return true;
    else
        return false;
}

bool Service::StopAndWait()
{
    Status status;
    if (Stop())
        return WaitPending(&status, SERVICE_STOP_PENDING);
    else
        return false;
}

bool Service::WaitPending(Status *status, DWORD existing_state)
{
    ATLTRACE(_T("Enter Service::WaitPending\n"));
    if (GetStatus(status))
    {
        DWORD dwStartTickCount = GetTickCount();
        DWORD dwOldCheckPoint = status->dwCheckPoint;

        while (status->dwCurrentState == existing_state)
        {
            ATLTRACE(_T("Service::WaitPending in loop\n"));
            DWORD dwWaitTime = status->dwWaitHint / 10;
            if (dwWaitTime < 1000)
                dwWaitTime = 1000;
            else if (dwWaitTime > 10000)
                dwWaitTime = 10000;

            ATLTRACE(_T("Sleeping\n"));
            ::Sleep(dwWaitTime);

            if (!GetStatus(status))
            {
                ATLTRACE(_T("Service::WaitPending - Failed to get status\n"));
                return false;
            }

            // If we haven't changed state yet then check to see that the
            // service is actually making progress.
            if (status->dwCurrentState == existing_state)
            {
                if (status->dwCheckPoint != dwOldCheckPoint)
                {
                    // The service is making progress
                    dwStartTickCount = GetTickCount();
                    dwOldCheckPoint = status->dwCheckPoint;
                }
                else if (GetTickCount() - dwStartTickCount > status->dwWaitHint)
                {
                    ATLTRACE(_T("Service::WaitPending - No progress\n"));
                    /// Hmm. No progress
                    return false;
                }
            }
        }
    }
    ATLTRACE(_T("Service::WaitPending success\n"));
    return true;
}

CString Service::GetBinaryPath() const
{
    CString path;

    DWORD bytes_required;
    ::QueryServiceConfig(m_sc_service, NULL, 0, &bytes_required);
    if (GetLastError() == ERROR_INSUFFICIENT_BUFFER)
    {
        void *buffer = operator new(bytes_required);
        LPQUERY_SERVICE_CONFIG config = reinterpret_cast<LPQUERY_SERVICE_CONFIG>(buffer);

        if (::QueryServiceConfig(m_sc_service, config, bytes_required, &bytes_required))
        {
            path = config->lpBinaryPathName;
        }

        delete buffer;
    }
    return path;
}

DWORD Service::GetStartup() const
{
    DWORD result = 0xffffffff;
    const size_t BUFFER_SIZE = 8192;
    void *buffer = operator new(BUFFER_SIZE);
    LPQUERY_SERVICE_CONFIG config = reinterpret_cast<LPQUERY_SERVICE_CONFIG>(buffer);

    DWORD bytes_required;
    if (::QueryServiceConfig(m_sc_service, config, BUFFER_SIZE, &bytes_required))
        result = config->dwStartType;

    delete buffer;
    return result;
}

bool Service::ConfigureStartup(DWORD startup) {
    if(startup != GetStartup()) { // don't boost privs if we don't need to 
        if(startup == SERVICE_AUTO_START) {
            return ExecHelper(_T("auto"));
        } else {
            return ExecHelper(_T("manual"));
        }
    }
    return true;
}

void ServiceStatusMonitor::Poll(Service *service)
{
    Service::Status new_status;
    if (service->GetStatus(&new_status))
    {
        if (!m_service_status.IsValid() || (m_service_status != new_status))
        {
            FireServiceStatus(m_service_status, new_status);
            m_service_status = new_status;
        }
    }
}
