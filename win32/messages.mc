;#ifndef __MESSAGES_H__
;#define __MESSAGES_H__
;
;
; // Eventlog messages?  What's this rubbish?  I'll just make a 
; // single message so I can reduce the eventlog api to syslog(3).  <sigh>
; // Perhaps this isn't as win32ish as it could be.  :)
;

LanguageNames =
    (
        English = 0x0409:Messages_ENU
    )


;////////////////////////////////////////
;// Events
;//

MessageId       = +1
SymbolicName    = EVENT_MSG
Language        = English
%1
.

;
;#endif  //__MESSAGES_H__
;