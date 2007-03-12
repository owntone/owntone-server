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

#ifndef SERVICECONTROL_H
#define SERVICECONTROL_H

class Service
{
    SC_HANDLE m_sc_manager;
    SC_HANDLE m_sc_service;
    bool m_can_control;
    CString m_name;

public:

    class Status : public SERVICE_STATUS
    {
        bool WaitPending();

    public:
        Status()
        {
            SERVICE_STATUS *clearable_this = this;
            ZeroMemory(clearable_this, sizeof(SERVICE_STATUS));
        }

        bool operator==(const Status &r) const
        {
            const SERVICE_STATUS *lhs = this;
            const SERVICE_STATUS *rhs = &r;
            return memcmp(lhs, rhs, sizeof(SERVICE_STATUS)) == 0;
        }

        bool operator!=(const Status &r) const
        {
            const SERVICE_STATUS *lhs = this;
            const SERVICE_STATUS *rhs = &r;
            return memcmp(lhs, rhs, sizeof(SERVICE_STATUS)) != 0;
        }

        void AssertValid() const
        {
            // If we've been written then this shouldn't be zero.
            ATLASSERT(dwCurrentState != 0);
        }

        bool IsValid() const
        {
            return dwCurrentState != 0;
        }

        bool IsRunning() const
        {
            AssertValid();
            return dwCurrentState == SERVICE_RUNNING;
        }

        bool IsStopped() const
        {
            AssertValid();
            return dwCurrentState == SERVICE_STOPPED;
        }

        bool IsPaused() const
        {
            AssertValid();
            return dwCurrentState == SERVICE_PAUSED;
        }

        bool IsPending() const
        {
            AssertValid();
            switch (dwCurrentState)
            {
            case SERVICE_CONTINUE_PENDING:
            case SERVICE_PAUSE_PENDING:
            case SERVICE_START_PENDING:
            case SERVICE_STOP_PENDING:
                return true;
            default:
                return false;
            }
        }
    };

    Service() : m_sc_manager(NULL), m_sc_service(NULL), m_can_control(false) {}
    ~Service() { Close(); }

    bool IsOpen() const
    {
        return m_sc_service != NULL;
    }
    bool Open(const TCHAR *name);
    void Close();

    bool GetStatus(Status *status) const;
    
    bool Start();
    bool StartAndWait();
    bool Stop();
    bool StopAndWait();
    bool WaitPending(Status *status, DWORD existing_state);
    bool PollPending(Status *status, DWORD existing_state);

    bool CanControl() const
    {
        // For the time being - need to deal with running as a user that can't control.
        return IsOpen() && m_can_control;
    }

    CString GetBinaryPath() const;

    /// Pass SERVICE_AUTO_START, SERVICE_BOOT_START, SERVICE_DEMAND_START,
    /// SERVICE_DISABLED or SERVICE_SYSTEM_START.
    bool ConfigureStartup(DWORD dwStartup);
    DWORD GetStartup() const;

private:
    bool ExecHelper(const TCHAR *szAction);
};

class ServiceStatusObserver
{
public:
    virtual void OnServiceStatus(Service::Status old_status, Service::Status new_status) = 0;
};

class ServiceStatusMonitor
{
    Service::Status m_service_status;
    std::vector<ServiceStatusObserver *> m_service_observers;

    void FireServiceStatus(Service::Status old_status, Service::Status new_status)
    {
        std::vector<ServiceStatusObserver *>::iterator i = m_service_observers.begin();
        while (i != m_service_observers.end())
        {
            (*i)->OnServiceStatus(old_status, new_status);
            ++i;
        }
    }

public:
    void Poll(Service *service);
    void Subscribe(ServiceStatusObserver *obs)
    {
        m_service_observers.push_back(obs);
    }

    void Unsubscribe(ServiceStatusObserver *obs)
    {
        std::vector<ServiceStatusObserver *>::iterator i = std::find(m_service_observers.begin(), m_service_observers.end(), obs);
        ATLASSERT(i != m_service_observers.end());
        if (i != m_service_observers.end())
        {
            m_service_observers.erase(i);
        }
    }

};
#endif // SERVICECONTROL_H
