:: All rights reserved.
::

@echo off
set PROJECT_ROOT=%CD%
set BUILD_TARGET=appimage
set BUILD_RELEASE_TYPE=debug
set PROJECT_OUT=%PROJECT_ROOT%\out\%BUILD_TARGET%_%BUILD_RELEASE_TYPE%

call :add_path %PROJECT_ROOT%\prebuilts\win32\bin
call :add_path %PROJECT_ROOT%\prebuilts\win32\cmake\bin
call :add_path %PROJECT_ROOT%\prebuilts\win32\python3
call :add_path %PROJECT_ROOT%\prebuilts\win32\gcc-arm-none-eabi\bin
call :add_path %PROJECT_ROOT%\tools
call :add_path %PROJECT_ROOT%\tools\win32

if not exist %PROJECT_OUT% mkdir %PROJECT_OUT%

exit /B 0

:add_path
(echo ";%PATH%;" | find /C /I ";%1;" > nul) || set "PATH=%1;%PATH%"
goto :eof
