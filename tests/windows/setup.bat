@echo off
rem Builds the Shalimar runtime on the Windows box, for tests\remote-windows.sh.
rem
rem The suite cross-compiles on the Mac and sends only assembly, so nothing here
rem reads the compiler's tree - but the runtime is C++ and has to be built by cl
rem on the machine that will link against it. It is rebuilt every run: it is six
rem translation units and a few seconds, and the alternative is a suite that
rem silently tests the previous runtime against this compiler's calls.
rem
rem **This file is in the repository on purpose.** It and build.bat used to live
rem only on the box, under C:\shalimar, and the rebuild of 2026-08-25 took them
rem with it - the suite then failed with "remote mkdir: No such file or
rem directory", which reads as a network fault rather than a missing scaffold.
rem remote-windows.sh now sends both every run, so a freshly built box needs
rem nothing done to it by hand.
rem
rem The flags are build.bat's, exactly: this project is ISO C++14 on all three
rem toolchains and cl is the third of them.
setlocal

if not "%VSCMD_ARG_TGT_ARCH%"=="x64" call :findvcvars
if errorlevel 1 exit /b 1

cd /d %~dp0
if not exist obj mkdir obj

cl /nologo /std:c++14 /W4 /WX /EHsc /permissive- /O2 /D_CRT_SECURE_NO_WARNINGS ^
   /Fo:obj\ /c ^
   runtime\Shortest.cpp runtime\Failure.cpp runtime\Numbers.cpp ^
   runtime\Array.cpp runtime\Console.cpp runtime\Runtime.cpp
if errorlevel 1 exit /b 1

lib /nologo /out:shmrt.lib ^
   obj\Shortest.obj obj\Failure.obj obj\Numbers.obj ^
   obj\Array.obj obj\Console.obj obj\Runtime.obj
if errorlevel 1 exit /b 1

rem remote-windows.sh greps for this word and stops if it is absent, so it must
rem be the last thing said and must not be said on any failing path.
echo RUNTIME_BUILT
exit /b 0

:findvcvars
rem Pinned to Visual Studio 2022 - the [17.0,18.0) below - for build.bat's
rem reason: a bare "vswhere -latest" reaches past it to a newer Visual Studio
rem if one is installed, which is not the toolset this is built with.
set VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe
if not exist "%VSWHERE%" (
    echo no vswhere - Visual Studio 2022 does not look installed
    exit /b 1
)
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -version [17.0^,18.0^) -property installationPath`) do set VSPATH=%%i
if "%VSPATH%"=="" (
    echo no Visual Studio 2022 found
    exit /b 1
)
call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul
exit /b 0
