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

bool Service::Open(const TCHAR *name)
{
	Close();

	const DWORD ADMIN_ACCESS = SC_MANAGER_ALL_ACCESS;
	const DWORD USER_ACCESS = SC_MANAGER_CONNECT | SC_MANAGER_ENUMERATE_SERVICE | STANDARD_RIGHTS_READ;

	DWORD dwDesiredAccess = ADMIN_ACCESS;

	m_sc_manager = ::OpenSCManager(NULL, SERVICES_ACTIVE_DATABASE, dwDesiredAccess);
	if (!m_sc_manager)
	{
		dwDesiredAccess = USER_ACCESS;
		m_sc_manager = ::OpenSCManager(NULL, SERVICES_ACTIVE_DATABASE, dwDesiredAccess);
		m_can_control = false;
	}
	else
	{
		m_can_control = true;
	}

	if (m_sc_manager)
	{
		m_sc_service = ::OpenService(m_sc_manager, name, dwDesiredAccess);
		if (m_sc_service)
			return true;
	}

	Close();
	return false;
}

void Service::Close()
{
	if (m_sc_service)
	{
		::CloseServiceHandle(m_sc_service);
		m_sc_service = NULL;
	}
	if (m_sc_manager)
	{
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
	if (::StartService(m_sc_service, 0, NULL))
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
	if (::ControlService(m_sc_service, SERVICE_CONTROL_STOP, &status))
		return true;
	else
		return false;
}

bool Service::StopAndWait()
{
	Status status;
	if (::ControlService(m_sc_service, SERVICE_CONTROL_STOP, &status))
	{
		return WaitPending(&status, SERVICE_STOP_PENDING);
	}
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

bool Service::ConfigureStartup(DWORD startup)
{
	if (::ChangeServiceConfig(m_sc_service, SERVICE_NO_CHANGE, startup, SERVICE_NO_CHANGE, NULL, NULL, NULL, NULL, NULL, NULL, NULL))
		return true;
	else
		return false;
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
