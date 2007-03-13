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

#pragma once

// Change these values to use different versions
#define WINVER		0x0400
#define _WIN32_WINNT 0x0600
#define _WIN32_IE	0x0500
#define _RICHEDIT_VER	0x0100

#define _UNICODE
#define UNICODE

// Visual C++ 2005's new secure string operations and deprecation
#define USE_SECURE 0
#if defined(_MSC_VER)
#if _MSC_VER >= 1400
#undef USE_SECURE
#define USE_SECURE 1
#define _CRT_SECURE_NO_DEPRECATE
#endif // _MSC_VER >= 1400
#endif // defined(_MSC_VER)

#include <atlbase.h>
#include <atlapp.h>

extern CAppModule _Module;

#include <atlwin.h>
#include <atlframe.h>
#include <atlctrls.h>
#include <atldlgs.h>
#include <atlmisc.h>
#include <atlddx.h>
#include <atlcrack.h>
#include <atlctrlx.h>
#include <vector>
#include <algorithm>

// WtsApi32.h is only in the latest platform SDK. Don't rely
// on everyone having it.
#if !defined(NOTIFY_FOR_THIS_SESSION)
#define NOTIFY_FOR_THIS_SESSION 0
#endif

#if _MSC_VER >= 1400
#if defined _M_IX86
  #pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='x86' publicKeyToken='6595b64144ccf1df' language='*'\"")
#elif defined _M_IA64
  #pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='ia64' publicKeyToken='6595b64144ccf1df' language='*'\"")
#elif defined _M_X64
  #pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='amd64' publicKeyToken='6595b64144ccf1df' language='*'\"")
#else
  #pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#endif
#endif

inline void SafeStringCopy(TCHAR *dest, const TCHAR *source, size_t len)
{
#if USE_SECURE
	_tcsncpy_s(dest, len, source, len);
#else
	_tcsncpy(dest, source, len);
	dest[len - 1] = 0;
#endif
}