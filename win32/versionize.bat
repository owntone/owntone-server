@echo off

set SUBWC="c:\program files\tortoisesvn\bin\subwcrev.exe"
if not exist %SUBWC% goto NOSUBWC

echo Fixing version info...
%SUBWC% %0\..\.. %0\..\config.h.templ %0\..\config.h 
%SUBWC% %0\..\.. %0\..\FireflyConfig\AssemblyInfo.cs.templ %0\..\FireflyConfig\AssemblyInfo.cs
%SUBWC% %0\..\.. %0\..\nsi\mt-daapd.nsi.templ %0\..\nsi\mt-daapd.nsi
goto END

:NOSUBWC
copy %0\..\config.h.templ %0\..\config.h
copy %0\..\FireflyConfig\AssemblyInfo.cs.templ %0\..\FireflyConfig\AssemblyInfo.cs 
copy %0\..\nsi\mt-daapd.nsi.templ %0\..\nsi\mt-daapd.nsi
:END
