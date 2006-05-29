@echo off

set SUBWC="c:\program files\tortoisesvn\bin\subwcrev.exe"
if not exist %SUBWC% set SUBWC=copy

echo Fixing version info...
%SUBWC% %0\..\.. %0\..\config.h.templ %0\..\config.h 
%SUBWC% %0\..\.. %0\..\FireflyConfig\AssemblyInfo.cs.templ %0\..\FireflyConfig\AssemblyInfo.cs
%SUBWC% %0\..\.. %0\..\nsi\mt-daapd.nsi.templ %0\..\nsi\mt-daapd.nsi
%SUBWC% %0\..\.. %0\..\ssc-ffmpeg.rc.templ %0\..\ssc-ffmpeg.rc
%SUBWC% %0\..\.. %0\..\mt-daapd.rc.templ %0\..\mt-daapd.rc
%SUBWC% %0\..\.. %0\..\rsp.rc.templ %0\..\rsp.rc
%SUBWC% %0\..\.. %0\..\w32-event.rc.templ %0\..\w32-event.rc
%SUBWC% %0\..\.. %0\..\FireflyShell\version.h.templ %0\..\FireflyShell\version.h
