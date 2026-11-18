@echo off
rem Builds SlowTimePitchFix.dll with the MSVC x64 toolset (Visual Studio 2022 or Build Tools 2022).
setlocal
cd /d "%~dp0"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR ( echo MSVC x64 toolset not found & exit /b 1 )
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
if not exist build mkdir build
cl /nologo /LD /O2 /MT /EHsc /W4 /WX /permissive- /std:c++17 /Fobuild\ src\SlowTimePitchFix.cpp /link /NOLOGO shell32.lib ole32.lib /OUT:build\SlowTimePitchFix.dll /IMPLIB:build\SlowTimePitchFix.lib || exit /b 1
cl /nologo /O2 /MT /EHsc /W4 /WX /permissive- /std:c++17 /Isrc /Fobuild\ tools\altest.cpp /link /NOLOGO /OUT:build\altest.exe || exit /b 1
copy /y build\SlowTimePitchFix.dll package\SKSE\Plugins\SlowTimePitchFix.dll >nul || exit /b 1
echo built: build\SlowTimePitchFix.dll
