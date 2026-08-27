@echo off
rem Runs build.bat in the directory this file sits in, so that the caller does
rem not have to change directory over ssh first.
rem
rem That sounds like a nicety and is not. `ssh box "cd DIR; cmd /c build.bat"`
rem was written for a box whose ssh landed in PowerShell, where `;` separates
rem statements. The box rebuilt on 2026-08-25 defaults to cmd, where `;` is not
rem a separator at all and the nested quotes mangle besides. Naming a .bat by
rem its path and letting it find its own directory runs under either shell, so
rem tests\build-windows.sh no longer depends on which one the box has.
setlocal
cd /d %~dp0
call build.bat %1
exit /b %errorlevel%
