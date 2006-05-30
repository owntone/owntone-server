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
#include "LogPage.h"
#include "FireflyShell.h"
#include "IniFile.h"

LRESULT CLogPage::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
	LoadLog();
	return 0;
}

LRESULT CLogPage::OnRefresh(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
	LoadLog();
	return 0;
}

void CLogPage::LoadLog()
{
	CWaitCursor wait;

	const size_t MAX_LOG = 65000;

	IniFile ini(GetApplication()->GetConfigPath());

	CString filename = ini.GetString(_T("general"), _T("logfile"), _T(""));
	if (filename.IsEmpty())
	{
		CString str;
		str.LoadString(IDS_LOG_NOLOG);
		GetDlgItem(IDC_LOG).SetWindowText(str);
	}
	else
	{
		FILE *fp = _tfopen(filename, _T("r"));
		if (fp)
		{
			// Fathom the length
			fseek(fp, 0, SEEK_END);
			long len = ftell(fp);
			if (len > MAX_LOG)
			{
				len = MAX_LOG;
				fseek(fp, -len, SEEK_END);
			}
			else
			{
				fseek(fp, 0, SEEK_SET);
			}

			char *buffer = new char[len + 1];
			size_t nread = fread(buffer, 1, len, fp);
			ATLASSERT(nread < MAX_LOG);
			buffer[nread] = 0;
			fclose(fp);

			// Normalise the line endings. Not particularly efficient but
			// it does work. It would be nice if we could cheaply tell
			// CString to preallocate a certain size.
			CString log(_T("Log file: "));
			log += filename;
			log += _T("\r\n\r\n");

			size_t n = 0;
			while (n < nread)
			{
				switch (buffer[n])
				{
				case '\r':
					// Ignore
					break;
				case '\n':
					log += _T("\r\n");
					break;
				default:
					log += buffer[n];
					break;
				}
				++n;
			}

			GetDlgItem(IDC_LOG).SetWindowText(log);
			delete []buffer;
		}
		else
		{
			CString str;
			str.Format(IDS_LOG_OPENFAILED, filename);
			GetDlgItem(IDC_LOG).SetWindowText(str);
		}
	}
}

