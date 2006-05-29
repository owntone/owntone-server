/*
 * (C) 1997, 2006 Mike Crowe
 *
 * License: Do what you like with it except claim that you wrote it.
 */

#ifndef DOSPATH_H
#define DOSPATH_H

class CDosPath
{
	CString m_drive;
	CString m_dir;
	CString m_file;
	CString m_ext;

	void SplitPath(const TCHAR *buffer);

public:
	enum { PATH_ONLY = 1 };

	CDosPath(const TCHAR *pszPath, int nFlags = 0);
	CDosPath(const CDosPath &old);
	~CDosPath();

	CDosPath &operator|=(CDosPath &fullpath);
	CDosPath operator|(CDosPath &fullpath);

	CDosPath &operator=(const CDosPath &old);
	CString GetPath() const;
	CString GetPathOnly() const;
	void SetPath(const TCHAR *pszBuffer, int nFlags = 0);

	static CDosPath CurrentPath();
	static CDosPath AppPath();
	static CDosPath TempPath();
#ifdef _WINDOWS
	static CDosPath WindowsPath();
#endif

	CString GetFile()
	{
		return m_file;
	}
	CString GetExt()
	{
		return m_ext;
	}
};

#endif // DOSPATH_H