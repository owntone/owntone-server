@echo off

set SUBWC="c:\program files\tortoisesvn\bin\subwcrev.exe"
if not exist %SUBWC% set SUBWC=copy

echo Fixing version info...
%SUBWC% %0\..\.. %0\..\config.h.templ %0\..\config.h 
%SUBWC% %0\..\.. %0\..\nsi\mt-daapd.nsi.templ %0\..\nsi\mt-daapd.nsi
%SUBWC% %0\..\.. %0\..\ssc-ffmpeg.rc.templ %0\..\ssc-ffmpeg.rc
%SUBWC% %0\..\.. %0\..\mt-daapd.rc.templ %0\..\mt-daapd.rc
%SUBWC% %0\..\.. %0\..\rsp.rc.templ %0\..\rsp.rc
%SUBWC% %0\..\.. %0\..\w32-event.rc.templ %0\..\w32-event.rc
%SUBWC% %0\..\.. %0\..\FireflyShell\version.h.templ %0\..\FireflyShell\version.h
%SUBWC% %0\..\.. %0\..\ssc-wma\ssc-wma.rc.templ %0\..\ssc-wma\ssc-wma.rc
%SUBWC% %0\..\.. %0\..\out-daap\out-daap.rc.templ %0\..\out-daap\out-daap.rc

if exist %0\..\do_sig.cmd.templ %SUBWC% %0\..\.. %0\..\do_sig.cmd.templ %0\..\do_sig.cmd


