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
#include "ServerEvents.h"

ServerEvents::ServerEvents(Observer *obs) :
	m_thread(INVALID_HANDLE_VALUE),
	m_mailslot(INVALID_HANDLE_VALUE),
	m_obs(obs) 
{
}

ServerEvents::~ServerEvents()
{
	ATLASSERT(m_mailslot == INVALID_HANDLE_VALUE);
	ATLASSERT(m_thread == INVALID_HANDLE_VALUE);
}

bool ServerEvents::Start()
{
	ATLASSERT(m_mailslot == INVALID_HANDLE_VALUE);
	ATLASSERT(m_thread == INVALID_HANDLE_VALUE);
	ATLASSERT(m_obs != NULL);

	m_mailslot = ::CreateMailslot(_T("\\\\.\\mailslot\\FireflyMediaServer--67A72768-4154-417e-BFA0-FA9B50C342DE"), 0, MAILSLOT_WAIT_FOREVER, NULL);

	if (m_mailslot != INVALID_HANDLE_VALUE)
	{
		//m_thread = ::CreateThread(NULL, 0, &StaticThreadProc, this, 0, &thread_id);
		m_thread = (HANDLE)_beginthreadex(NULL, 0, &StaticThreadProc, this, 0, NULL);
		if (m_thread == NULL)
		{
			// Failed
			ATLTRACE("beginthreadex failed: %d\n", errno);
			::CloseHandle(m_mailslot);
			m_mailslot = INVALID_HANDLE_VALUE;
			return false;
		}
	}
	else
	{
		return false;
	}

	return true;
}

void ServerEvents::Stop()
{
	ATLASSERT(m_mailslot != INVALID_HANDLE_VALUE);
	ATLASSERT(m_thread != INVALID_HANDLE_VALUE);

	// Force the thread to give up. This could be done with a separate event
	// and overlapped IO but this is cheap.
	CloseHandle(m_mailslot);
	m_mailslot = INVALID_HANDLE_VALUE;

	// Wait for it to finish.
	WaitForSingleObject(m_thread, INFINITE);
	CloseHandle(m_thread);
	m_thread = INVALID_HANDLE_VALUE;
}

//DWORD ServerEvents::StaticThreadProc(LPVOID param)
unsigned ServerEvents::StaticThreadProc(void *param)
{
	return reinterpret_cast<ServerEvents *>(param)->ThreadProc();
}

DWORD ServerEvents::ThreadProc()
{
	const size_t BUFFER_SIZE = 65536;
	void *buffer = operator new(BUFFER_SIZE);

	bool finished = false;
	while (!finished)
	{
		DWORD bytes_read;
		if (ReadFile(m_mailslot, buffer, BUFFER_SIZE, &bytes_read, NULL))
		{
			TCHAR *b = (TCHAR *)buffer;
			b[bytes_read/sizeof(TCHAR)] = 0;
			ATLTRACE("%ls\n", b);

			OnEvent(buffer, bytes_read);
		}
		else
		{
			ATLTRACE("Read failed: error %d\n", GetLastError());
			finished = true;
		}
	}
	return 0;
}

void ServerEvents::OnEvent(const void *buffer, size_t bytes_received)
{
	const BYTE *received = reinterpret_cast<const BYTE *>(buffer);

	if (bytes_received >= 12)
	{
		UINT32 packet_size = received[0] | (received[1] << 8) | (received[2] << 16) | (received[3] << 24);
		UINT32 id = received[4] | (received[5] << 8) | (received[6] << 16) | (received[7] << 24);
		UINT32 intval = received[8] | (received[9] << 8) | (received[10] << 16) | (received[11] << 24);

		int string_length = static_cast<int>(bytes_received) - 12;

		if ((packet_size < bytes_received) && (packet_size >= 12))
			string_length = packet_size - 12;

		CString str;
		if (string_length > 0)
		{
#ifdef UNICODE
			// It might be less that string_length long after conversion but it shouldn't be more unless
			// our codepage is extremely weird.
			
			str.ReleaseBuffer(MultiByteToWideChar(CP_ACP, MB_PRECOMPOSED, reinterpret_cast<const char *>(received + 12),
				              string_length, str.GetBufferSetLength(string_length), string_length));
#else
			SafeStringCopy(str.GetBufferSetLength(string_length), received + 12, string_length);
#endif
			str.ReleaseBuffer(string_length);
		}

		m_obs->OnServerEvent(id, intval, str);
	}
}