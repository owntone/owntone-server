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

#ifndef SINGLETON_H
#define SINGLETON_H

template <class T>
class Singleton
{
	static T *sm_instance;

public:
	Singleton()
	{
		T *instance = static_cast<T *>(this);
		ATLASSERT(sm_instance == NULL);
		sm_instance = instance;
	}

	virtual ~Singleton()
	{
		ATLASSERT(sm_instance != NULL);
		sm_instance = NULL;
	}

	static T *GetInstance()
	{
		ATLASSERT(sm_instance != NULL);
		return sm_instance;
	}
};

template <class T>
T *Singleton<T>::sm_instance;

#endif // SINGLETON_H