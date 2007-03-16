// svcctrl.cpp : Defines the entry point for the application.
//

#include "stdafx.h"
#include "svcctrl.h"

#define MAX_LOADSTRING 100

#define E_SVC_SUCCESS    0
#define E_SVC_BADARGS    1
#define E_SVC_NORIGHTS   2
#define E_SVC_CANTSTART  3
#define E_SVC_CANTSTOP   4
#define E_SVC_CANTCONFIG 5

// Global Variables:
HINSTANCE hInst;								// current instance

// code re-use via cut and paste.  woohoo.  This from fellow.sourceforge.net

/*===========================================================================*/
/* Command line conversion routines                                          */
/*===========================================================================*/

/* Returns the first character in the next argument                          */

char *winDrvCmdLineGetNextFirst(char *lpCmdLine) {
  while (*lpCmdLine == ' ' && *lpCmdLine != '\0')
    lpCmdLine++;
  return (*lpCmdLine == '\0') ? NULL : lpCmdLine;
}


/* Returns the first character after the next argument                       */

char *winDrvCmdLineGetNextEnd(char *lpCmdLine) {
  int InString = FALSE;
  
  while (((*lpCmdLine != ' ') && (*lpCmdLine != '\0')) ||
    (InString && (*lpCmdLine != '\0'))) {
    if (*lpCmdLine == '\"')
      InString = !InString;
    lpCmdLine++;
  }
  return lpCmdLine;
}

/* Returns an argv vector and takes argc as a pointer parameter              */
/* Must free memory argv on exit                                             */

char **winDrvCmdLineMakeArgv(char *lpCmdLine, int *argc) {
  int elements = 0, i;
  char *tmp;
  char **argv;
  char *argstart, *argend;
  
  tmp = winDrvCmdLineGetNextFirst(lpCmdLine);
  if (tmp != 0) {
    while ((tmp = winDrvCmdLineGetNextFirst(tmp)) != NULL) {
      tmp = winDrvCmdLineGetNextEnd(tmp);
      elements++;
    }
  }
  argv = (char **) malloc(4*(elements + 2));
  argv[0] = "svcctrl.exe";
  argend = lpCmdLine;
  for (i = 1; i <= elements; i++) {
    argstart = winDrvCmdLineGetNextFirst(argend);
    argend = winDrvCmdLineGetNextEnd(argstart);
    if (*argstart == '\"')
      argstart++;
    if (*(argend - 1) == '\"')
      argend--;
    *argend++ = '\0';
    argv[i] = argstart;
  }
  argv[elements + 1] = NULL;
  *argc = elements + 1;
  return argv;
}

int APIENTRY _tWinMain(HINSTANCE hInstance,
                     HINSTANCE hPrevInstance,
                     LPTSTR    lpCmdLine,
                     int       nCmdShow) {
    
    char *cmdline = _strdup(lpCmdLine);
    char **argv;
    int items;
    int retval=0;

    SC_HANDLE scm;
    SC_HANDLE svc;

    SERVICE_STATUS status;

    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    argv=winDrvCmdLineMakeArgv(cmdline,&items);
    if(items != 3)
        return E_SVC_BADARGS;


    if(!(scm = OpenSCManager(0,0,SC_MANAGER_ALL_ACCESS))) {
        return E_SVC_NORIGHTS;
    }

    if(!(svc = OpenService(scm, argv[2],SC_MANAGER_ALL_ACCESS))) {
        CloseServiceHandle(scm);
        return E_SVC_NORIGHTS;
    }

    if(!strcmp(argv[1],"start")) {
        if(!StartService(svc,0,NULL)) {
            retval = E_SVC_CANTSTART;
        }
    }

    if(!strcmp(argv[1],"stop")) {
        if(!ControlService(svc,SERVICE_CONTROL_STOP,&status)) {
            retval = E_SVC_CANTSTOP;
        }
    }

    if(!strcmp(argv[1],"manual")) {
        if (!ChangeServiceConfig(svc, SERVICE_NO_CHANGE, SERVICE_DEMAND_START, SERVICE_NO_CHANGE, NULL, NULL, NULL, NULL, NULL, NULL, NULL))
            retval = E_SVC_CANTCONFIG;
    }

    if(!strcmp(argv[1],"auto")) {
        if (!ChangeServiceConfig(svc, SERVICE_NO_CHANGE, SERVICE_AUTO_START, SERVICE_NO_CHANGE, NULL, NULL, NULL, NULL, NULL, NULL, NULL))
            retval = E_SVC_CANTCONFIG;
    }

    
    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return retval;
}
