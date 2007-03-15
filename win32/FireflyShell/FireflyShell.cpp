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

#include <winnls.h>

#include "resource.h"
#include "FireflyShell.h"
#include "DosPath.h"
#include "ServiceControl.h"
#include "MainDlg.h"
#include "ServerEvents.h"
#include "IniFile.h"

CAppModule _Module;

#define RUN_KEY _T("SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run")
#define RUN_VALUE _T("FireflyShell")

Application::Application()
: m_dlg(NULL), m_server_events(&m_icon)
{
    CDosPath path = CDosPath::AppPath();
    SetCurrentDirectory(path.GetPathOnly());
    CDosPath filename(_T("mt-daapd.conf"));
    filename |= path;
    m_config_path = filename.GetPath();
    ATLTRACE("Config path: %s\n", (const TCHAR *)m_config_path);


    // Dump an ini file with drive mappings
    CDosPath mapfile(_T("mapping.ini"));
    mapfile |= path;
    m_ini_path = mapfile.GetPath();

    /* Load the proper language dll, if possible */
    LANGID lidDefault = GetUserDefaultLangID();
    int iBaseLanguage = lidDefault & 0xFF;
    iBaseLanguage = GetPrivateProfileInt(_T("shell"),_T("lang_id"),iBaseLanguage,m_ini_path);

    TCHAR tempPath[24];
    wsprintf(tempPath,_T("FireflyShell-%02x.dll"),iBaseLanguage);
    CDosPath langlib(tempPath);
    langlib |= path;

    HMODULE hLangDLL;
    if((hLangDLL=LoadLibrary(langlib.GetPath())) != NULL) {
        _AtlBaseModule.SetResourceInstance(hLangDLL);
    }
   
    TCHAR inbuffer[4];
    TCHAR outbuffer[2048]; /* as max_path is unreliable */
    DWORD size;
    UNIVERSAL_NAME_INFO * puni = (UNIVERSAL_NAME_INFO *) &outbuffer;

    for(int x='A'; x<='Z'; x++) {
        wsprintf(inbuffer,_T("%c:\\"),x);
        memset(outbuffer,0,sizeof(outbuffer));
        size = sizeof(outbuffer);
        WNetGetUniversalName(inbuffer,UNIVERSAL_NAME_INFO_LEVEL,outbuffer,
            &size);
        wsprintf(inbuffer,_T("%c"),x);
        WritePrivateProfileString(_T("mapping"),inbuffer,puni->lpUniversalName,
            mapfile.GetPath());
    }


    // We don't care if this fails. We can deal with that later.
    m_service.Open(_T("Firefly Media Server"));

    CheckCanConfigure();

    m_unique_name = GenerateUniqueName();
    m_registered_activation_message = ::RegisterWindowMessage(m_unique_name);
}

Application::~Application()
{
    ATLASSERT(m_dlg == NULL);
}

void Application::CheckCanConfigure()
{
    IniFile ini(m_config_path);
    m_configurable = ini.IsWritable();
}

int Application::Run(LPCTSTR lpstrCmdLine, int nCmdShow)
{
    if (ActivatePreviousInstance(lpstrCmdLine, nCmdShow))
    {
        ATLTRACE(_T("Already running\n"));
        return 0;
    }

    CMessageLoop theLoop;
    _Module.AddMessageLoop(&theLoop);

    if (!m_icon.Create())
    {
        ATLTRACE(_T("Icon creation failed!\n"));
        return 0;
    }

    EnableServerEvents(true);

    if (ShowDialogAtStart(lpstrCmdLine, nCmdShow))
        Configure(false);

    int nRet = theLoop.Run();

    EnableServerEvents(false);

    m_icon.Destroy();

    _Module.RemoveMessageLoop();
    return nRet;
}

void Application::Exit()
{
    if (m_dlg)
    {
        m_dlg->DestroyWindow();
    }
    ::PostQuitMessage(0);
}

void Application::Configure(bool move_window)
{
    if (m_dlg)
    {
        m_dlg->ShowWindow(SW_RESTORE);
        SetForegroundWindow(m_dlg->m_hWnd);
    }
    else
    {
        CheckCanConfigure();

        // Other people may need to talk to the dialog while it exists.
        CMainDlg dlg(move_window);
        m_dlg = &dlg;
        dlg.DoModal();
        m_dlg = NULL;
    }
}

void Application::StartService(HWND hwndParent)
{
    CWaitCursor wc;
    ATLASSERT(m_service.CanControl());
    if (!m_service.CanControl())
        return;

    if (!m_service.StartAndWait())
    {
        MessageBox(hwndParent, IDS_SERVERSTARTFAIL, MB_OK);
    }
}

void Application::StopService(HWND hwndParent)
{
    CWaitCursor wc;
    ATLASSERT(m_service.CanControl());
    if (!m_service.CanControl())
        return;

    if (!m_service.StopAndWait())
    {
        MessageBox(hwndParent, IDS_SERVERSTOPFAIL, MB_OK);
    }
}

void Application::RestartService(HWND hwndParent)
{
    CWaitCursor wc;
    StopService(hwndParent);
    StartService(hwndParent);
}

bool Application::ShowDialogAtStart(LPCTSTR cmdline, int nCmdShow)
{
    if ((cmdline[0] == '-') && (cmdline[1] == 'q'))
        return false;

    switch (nCmdShow)
    {
    case SW_RESTORE:
    case SW_SHOW:
    case SW_SHOWMAXIMIZED:
    case SW_SHOWNORMAL:
    case SW_SHOWDEFAULT:
    case SW_MAX:
        return true;

    default:
        return false;
    }
}

BOOL CALLBACK Application::StaticWindowSearcher(HWND hwnd, LPARAM lparam)
{
    DWORD result;
    LRESULT ok = ::SendMessageTimeout(hwnd,
        static_cast<UINT>(lparam),
        0, 0, 
        SMTO_BLOCK |
        SMTO_ABORTIFHUNG,
        200,
        &result);
    if (ok == 0)
        return TRUE; // ignore this and continue

    // If we get the magic response then we must have found our Window
    // so we can give up.
    if (result == lparam)
        return FALSE;
    else
        return TRUE;
}

bool Application::ActivatePreviousInstance(LPCTSTR lpstrCmdLine, int nCmdShow)
{
    HANDLE h = ::CreateMutex(NULL, TRUE, m_unique_name);
    const bool running = (GetLastError() == ERROR_ALREADY_EXISTS);

    if (h != NULL) 
    {
        ::ReleaseMutex(h);
    }

    // It seems that getting the other window to activate itself does
    // actually work even though Windows anti-focus-stealing stuff
    // could try and stop it.
    if (running && ShowDialogAtStart(lpstrCmdLine, nCmdShow))
    {
        EnumWindows(StaticWindowSearcher, m_registered_activation_message);
        return true;
    }

    return running;
}

CString Application::GenerateUniqueName()
{
    // We need to allow one instance to run per desktop. See
    // http://www.codeproject.com/cpp/avoidmultinstance.asp

    // First start with some application unique
    CString s(_T("Firefly-67A72768-4154-417e-BFA0-FA9B50C342DE"));

    // Now append something desktop unique
    DWORD len;
    HDESK desktop = GetThreadDesktop(GetCurrentThreadId());
    BOOL result = GetUserObjectInformation(desktop, UOI_NAME, NULL, 0, &len);
    DWORD err = ::GetLastError();
    if(!result && err == ERROR_INSUFFICIENT_BUFFER)
    { /* NT/2000/XP */
        LPBYTE data = new BYTE[len];
        result = GetUserObjectInformation(desktop, UOI_NAME, data, len, &len);
        s += _T("-");
        s += (LPCTSTR)data;
        delete [ ] data;
    } /* NT/2000/XP */
    else
    { /* Win9x */
        s += _T("-Win9x");
    } /* Win9x */
    return s;
}

void Application::EnableServerEvents(bool b)
{
    if (b)
        m_server_events.Start();
    else
        m_server_events.Stop();
}

CString Application::MakeRunKeyValue()
{
    CString required_path("\"");
    required_path += CDosPath::AppPath().GetPath();
    required_path += "\" -q";
    return required_path;
}

void Application::EnableServiceAutoStart(HWND hwnd, bool enable) {
    int required_startup = enable ? SERVICE_AUTO_START : SERVICE_DEMAND_START;

    if (m_service.GetStartup() != required_startup) {
        if (!m_service.ConfigureStartup(required_startup)) {
            MessageBox(hwnd, IDS_FAILED_CONFIGURE_SERVICE, MB_OK);
        }
    }
}

void Application::EnableAppletAutoStart(HWND hwnd, bool enable) {
    HKEY hkey;
    LONG result = ::RegOpenKeyEx(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_SET_VALUE | STANDARD_RIGHTS_WRITE, &hkey);
    if (result == ERROR_SUCCESS) {
        if (enable) {
            // We need to quote it because the path may contain spaces.
            CString str = MakeRunKeyValue();
            result = RegSetValueEx(hkey, RUN_VALUE, 0UL, REG_SZ, reinterpret_cast<LPCBYTE>(static_cast<LPCTSTR>(str)), (str.GetLength() + 1) * sizeof(TCHAR));
        } else {
            result = RegDeleteValue(hkey, RUN_VALUE);
            if (result == ERROR_FILE_NOT_FOUND)
                result = 0;
        }

        if (result != ERROR_SUCCESS) {
            ATLTRACE("Error:%u\n", result);
            MessageBox(hwnd, IDS_FAILED_CONFIGURE_STARTUP, MB_OK);
        }
    }
}

bool Application::IsServiceAutoStartEnabled() const {
    return (m_service.GetStartup() == SERVICE_AUTO_START);
}

bool Application::IsAppletAutoStartEnabled() const {
    bool run_result = false;
    HKEY hkey;
    if (::RegOpenKeyEx(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_QUERY_VALUE | STANDARD_RIGHTS_READ, &hkey) == ERROR_SUCCESS)
    {
        DWORD dwType, cbData;
        TCHAR buffer[_MAX_PATH + 1];
        cbData = (_MAX_PATH + 1) * sizeof(TCHAR);
        if (::RegQueryValueEx(hkey, RUN_VALUE, NULL, &dwType, reinterpret_cast<LPBYTE>(buffer), &cbData) == ERROR_SUCCESS)
        {
            CString path(buffer, cbData - sizeof(TCHAR));
            ATLTRACE("Registry run key path: %s\n", (const TCHAR *)path);

            if (path == MakeRunKeyValue())
                run_result = true;
            else
            {
                // It's there - but it isn't us that it will start.

                // But from the user's perspective, it *is* us, so to make the perception
                // match the setting, we'll make this true, rather than some indeterminate 
                // thing which nobody understands.          -- Ron                           
                
                run_result = true;
            }
        }
        else
        {
            // The key doesn't exist.
            run_result = false;
        }
    }
    else
        run_result = false;

    return run_result;
}

int WINAPI _tWinMain(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/, LPTSTR lpstrCmdLine, int nCmdShow)
{
    // this resolves ATL window thunking problem when Microsoft Layer for Unicode (MSLU) is used
    ::DefWindowProc(NULL, 0, 0, 0L);

    AtlInitCommonControls(ICC_BAR_CLASSES);	// add flags to support other controls

    HRESULT hRes = _Module.Init(NULL, hInstance);
    ATLASSERT(SUCCEEDED(hRes));

    int nRet;
    {
        // Application object is destroyed prior to undoing everything above.
        Application app;
        nRet = app.Run(lpstrCmdLine, nCmdShow);
    }

    _Module.Term();
    return nRet;
}
