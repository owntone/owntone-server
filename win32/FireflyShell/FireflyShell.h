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

#ifndef FIREFLYSHELL_H
#define FIREFLYSHELL_H 1

#include "singleton.h"
#include "NotifyIcon.h"
#include "ServiceControl.h"
#include <vector>
#include <algorithm>
#include "ServerEvents.h"

class CMainDlg;

/// The main application. Not a window.
class Application : public Singleton<Application>
{
	CNotifyIcon m_icon;
	CMainDlg *m_dlg;
	CString m_config_path;
        CString m_ini_path;
	Service m_service;
	ServiceStatusMonitor m_service_monitor;
	ServerEvents m_server_events;
	CString m_unique_name;
	UINT m_registered_activation_message;
	bool m_configurable;

	/// Returns true if a previous instance was found. Only
	/// actually activates a previous instance if nCmdShow
	/// indicates so.
	bool ActivatePreviousInstance(LPCTSTR cmdline, int nCmdShow);

	/// Helper for ActivatePreviousInstance.
	static BOOL CALLBACK Application::StaticWindowSearcher(HWND hwnd, LPARAM lparam);

	/// Generate a unique name that can be used to detect multiple instances.
	static CString GenerateUniqueName();

	// Depending on how the application was launched we display the dialog or not.
	static bool ShowDialogAtStart(LPCTSTR cmdline, int nCmdShow);

	static CString MakeRunKeyValue();

public:
	Application();
	~Application();

	/// Gets the application going
	int Run(LPCTSTR lpstrCmdLine, int nCmdShow);

	/// mt-daapd.conf path
	CString GetConfigPath() 
	{ 
		return m_config_path; 
	}

	/// Registered message used to activate other instances
	UINT GetRegisteredActivationMessage() const
	{
		return m_registered_activation_message;
	}

	// Pass true to move the window so it is near the mouse
	// cursor.
	void Configure(bool move_window);
	void Exit();

	// Service control
	void StartService(HWND hwndParent);
	void StopService(HWND hwndParent);
	void RestartService(HWND hwndParent);

	Service::Status GetServiceStatus()
	{
		Service::Status status;
		if (m_service.IsOpen())
		{
			m_service.GetStatus(&status);
		}
		return status;
	}

	// Expensive - only do it just before displaying the dialog box.
	void CheckCanConfigure();

	// Cheap.
	bool CanConfigure() const
	{
		// We need to both rewrite the config file and
		// control the service if we're going to be
		// useful.
		return m_configurable && m_service.CanControl();
	}

	bool CanControlService() const
	{
		return m_service.CanControl();
	}

	void CheckServiceStatus()
	{
		m_service_monitor.Poll(&m_service);
	}

	void ServiceStatusSubscribe(ServiceStatusObserver *obs)
	{
		m_service_monitor.Subscribe(obs);
	}

	void ServiceStatusUnsubscribe(ServiceStatusObserver *obs)
	{
		m_service_monitor.Unsubscribe(obs);
	}

	CString GetServiceBinaryPath() const
	{
		return m_service.GetBinaryPath();
	}

	/// Used to disable listening for events from the server when this user is
	/// not active.
	void EnableServerEvents(bool);

	/// Enable/disable automatic startup of service and FireflyShell.
	void EnableAutoStart(HWND, bool);

	/// Reports 0 for disabled, 1 for enabled, 2 for indeterminate
	int IsAutoStartEnabled() const;

	int MessageBox(HWND hwnd, UINT id, UINT flags)
	{
		CString title, text;
		ATLVERIFY(title.LoadString(IDR_MAINFRAME));
		ATLVERIFY(text.LoadString(id));
		return ::MessageBox(hwnd, text, title, flags);
	}
};

inline Application *GetApplication() { return Application::GetInstance(); }

#endif // FIREFLYSHELL_H
