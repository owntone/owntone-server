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

#ifndef SERVEREVENTS_H
#define SERVEREVENTS_H

class ServerEvents
{
public:
	/// Note that the observer is called on the wrong thread.
	class Observer
	{
	public:
		virtual void OnServerEvent(UINT32 id, UINT32 intval, const CString &str) = 0;
	};

private:

	HANDLE m_thread;
	HANDLE m_mailslot;
	Observer *m_obs;

	static unsigned __stdcall StaticThreadProc(void *);
	DWORD ThreadProc();

	void OnEvent(const void *buffer, size_t length);

public:
	ServerEvents(Observer *obs);
	~ServerEvents();

	void SetObserver(Observer *obs)
	{
		ATLASSERT(m_obs == NULL);
		m_obs = obs;
	}

	bool Start();
	void Stop();
};

#endif // SERVICE