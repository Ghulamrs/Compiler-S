@echo off
rem Builds shc with MSVC, which is how it is built on the machine it is meant
rem for. There is no make on that box and none is needed: twenty translation
rem units, two small archives and one link.
rem
rem   build            builds shc.exe and both runtime archives
rem   build examples   builds it, then compiles and runs every example
rem   build clean      removes what a build leaves
rem
rem Run it from a Developer Command Prompt, or run it from anywhere and let it
rem find vcvars64 itself. shc needs that environment at run time as well as at
rem build time: it calls ml64 and link by their bare names, and those two ship
rem with Visual Studio and reach PATH only after vcvars64.bat has run.
rem
rem _CRT_SECURE_NO_WARNINGS is defined for the same reason Compiler-C's own
rem project defines it: getenv and strerror are standard C++, and MSVC's
rem objection to them is house policy rather than a defect to go and fix.
rem
rem /std:c++14 is not a nicety here. This project is ISO C++14 on all three
rem toolchains - see CLAUDE.md - and cl is the third of them.
setlocal

if not "%VSCMD_ARG_TGT_ARCH%"=="x64" call :findvcvars
if errorlevel 1 goto :fail

if "%1"=="clean" goto :clean

if not exist build\msvc mkdir build\msvc
if not exist build\msvc\debug mkdir build\msvc\debug
if not exist lib mkdir lib

echo building shc.exe
cl /nologo /std:c++14 /W4 /WX /EHsc /permissive- /O2 /D_CRT_SECURE_NO_WARNINGS ^
   /Fe:shc.exe /Fo:build\msvc\ ^
   src\main.cpp src\Driver.cpp src\Diag.cpp src\Lexer.cpp src\Type.cpp ^
   src\Ast.cpp src\Parser.cpp src\Check.cpp src\Resolve.cpp src\Builtin.cpp ^
   runtime\Shortest.cpp src\CodeGen.cpp src\Target.cpp ^
   src\backend\Emitter.cpp src\backend\Spelling.cpp src\backend\Arm64Darwin.cpp ^
   src\backend\X86_64.cpp src\backend\X86_64Linux.cpp src\backend\X86_64Windows.cpp
if errorlevel 1 goto :fail

rem The runtime, twice from the same sources. The release archive has no
rem debugger code in it at all; the debug one is the same program plus a
rem session that is dormant until SHM_DEBUG arms it. What the compiler emits
rem does not differ between them by a byte - see docs\DEBUGGING.md.
rem
rem Named shmrt-x86_64-windows.lib rather than shmrt.lib, because that is the
rem name the driver looks for beside itself: lib\shmrt-<target>[-debug].lib.
rem .lib and .a are not interchangeable, and naming the wrong one gets "cannot
rem open input file", which reads as a missing runtime rather than as one under
rem its other name.
echo building the runtime
cl /nologo /std:c++14 /W4 /WX /EHsc /permissive- /O2 /D_CRT_SECURE_NO_WARNINGS ^
   /Fo:build\msvc\ /c ^
   runtime\Shortest.cpp runtime\Failure.cpp runtime\Numbers.cpp ^
   runtime\Array.cpp runtime\Console.cpp runtime\Runtime.cpp
if errorlevel 1 goto :fail
lib /nologo /out:lib\shmrt-x86_64-windows.lib ^
   build\msvc\Shortest.obj build\msvc\Failure.obj build\msvc\Numbers.obj ^
   build\msvc\Array.obj build\msvc\Console.obj build\msvc\Runtime.obj
if errorlevel 1 goto :fail

echo building the runtime a debugger can stop
cl /nologo /std:c++14 /W4 /WX /EHsc /permissive- /O2 /D_CRT_SECURE_NO_WARNINGS ^
   /DSHM_DEBUG=1 /Fo:build\msvc\debug\ /c ^
   runtime\Shortest.cpp runtime\Failure.cpp runtime\Numbers.cpp ^
   runtime\Array.cpp runtime\Console.cpp runtime\Runtime.cpp runtime\Debug.cpp
if errorlevel 1 goto :fail
lib /nologo /out:lib\shmrt-x86_64-windows-debug.lib ^
   build\msvc\debug\Shortest.obj build\msvc\debug\Failure.obj ^
   build\msvc\debug\Numbers.obj build\msvc\debug\Array.obj ^
   build\msvc\debug\Console.obj build\msvc\debug\Runtime.obj ^
   build\msvc\debug\Debug.obj
if errorlevel 1 goto :fail

if "%1"=="examples" goto :examples
goto :done

:examples
rem Every example compiled and run. The .expected files are recorded from the
rem app's interpreter on a Mac and are compared there; this only asks whether
rem the program builds and runs on this machine, which is the question shc.exe
rem existing was meant to answer.
if not exist build\ran mkdir build\ran
set FAILED=0
for %%f in (examples\*.shm) do call :one %%f
if not "%FAILED%"=="0" (
    echo %FAILED% example^(s^) did not build or run
    exit /b 1
)
echo all examples built and ran
goto :done

:one
shc.exe %1 -o build\ran\%~n1.exe
if errorlevel 1 (
    echo FAIL %~n1 did not build
    set /a FAILED+=1
    goto :eof
)
build\ran\%~n1.exe >nul
if errorlevel 1 (
    echo FAIL %~n1 did not run
    set /a FAILED+=1
)
goto :eof

:clean
if exist build\msvc rmdir /s /q build\msvc
if exist build\ran rmdir /s /q build\ran
if exist shc.exe del shc.exe
if exist lib\shmrt-x86_64-windows.lib del lib\shmrt-x86_64-windows.lib
if exist lib\shmrt-x86_64-windows-debug.lib del lib\shmrt-x86_64-windows-debug.lib
echo cleaned
goto :done

:findvcvars
rem Pinned to Visual Studio 2022 - the [17.0,18.0) below. A bare
rem "vswhere -latest" reaches past it to a newer Visual Studio if one is
rem installed, which is not the toolset this is built with.
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

:fail
echo build failed
exit /b 1

:done
echo built shc.exe
exit /b 0
