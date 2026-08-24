@echo off

if "%1"=="-h" (
    echo parameters help:
    echo                     build project
    echo     [-t target]     target select, default target=hello
    echo     [-c]            clean project
    echo     [-d]            download
    echo     [-fs]           set flash size=[size]MB, default is 16MB, option is 8MB
    echo    [-bv version]    set module version:GW1NS
    exit /B 0
)

set folder_ab_current=%cd%
set para_ab_cmd=build
set para_ab_target=hello
set para_flash_size=2
set para_module_version=GW1NS

set folder_ab_build_out=%folder_ab_current%\out
set file_ab_build_config=%folder_ab_build_out%\bcfg 
set ctool_exec=%folder_ab_current%\..\Tools\ctools.exe
set cmd_ab_python=%folder_ab_current%\prebuilts\win32\python3\python3.exe

::load build config
if exist %file_ab_build_config% (
    for /f "tokens=1,2 delims='" %%a in (%file_ab_build_config%) do (
        if "%%a"=="target=" set para_ab_target=%%b
        if "%%a"=="flash=" set para_flash_size=%%b
        if "%%a"=="module_version=" set para_module_version=%%b
    )
)

::parse parameters
goto para_ab_parse
:para_ab_parse
if not "%1"=="" (
    if "%1"=="-c" (
        set para_ab_cmd=clean
    ) else if "%1"=="-d" (
        set para_ab_cmd=download
    ) else if "%1"=="-t" (
        set para_ab_target=%2
        shift /0
    ) else if "%1"=="-fs" (
        set para_flash_size=%2
        shift /0
    ) else if "%1"=="-bv" (
        set para_module_version=%2
        shift /0
    ) else (
        echo unknown para %1
        exit /B 1
    )

    shift /0
    goto para_ab_parse
)

echo ****************************************
echo cmd=%para_ab_cmd%
echo target=%para_ab_target%
echo module version=%para_module_version%
echo ****************************************

::check module version
@REM set module_kernel_path=%folder_ab_current%\..\kernel\%para_module_version%\
@REM echo %module_kernel_path%
@REM if not exist %module_kernel_path% (
@REM     echo module select error
@REM     exit /B 1
@REM )

::save previous build parameters
if not exist %folder_ab_build_out% (
    md %folder_ab_build_out%
)
echo target='%para_ab_target%'>%file_ab_build_config%
echo flash='%para_flash_size%'>>%file_ab_build_config%
echo module_version='%para_module_version%'>>%file_ab_build_config%
set module_version=%para_module_version%

if %para_module_version% == GW1NS (
    set VFP_EN=n
    if %para_flash_size% == 4 (
        set module_version=%para_module_version%-4
    ) else (
        echo "GW1NS flash size only support 4M"
        exit /B 1
    )
) else if %para_module_version% == ZX600CH (
    set module_version=%para_module_version%-8
) else (
  echo "no this version!"
        exit /B 1
)

::check para_ab_cmd
if "%para_ab_cmd%"=="clean" (
    call :clean
) else if "%para_ab_cmd%"=="build" (
    call :build
    if errorlevel 1 (
        exit /b 1
    )
) else if "%para_ab_cmd%"=="download" (
    call :download
) else (
    echo error para_ab_cmd!!!
    exit /B 1
)



exit /B 0

:build
    echo build target: %para_ab_target%

    if "%para_ab_target%"=="hello" (
        copy /y .\CMakeLists.txt.in .\CMakeLists.txt
        call :_build
        goto :EOF
    ) 

    set para_ab_example_name=%para_ab_target%
    set para_ab_example_suffix=
    if exist .\%para_ab_example_name%.c (
        set para_ab_example_suffix=c
    ) else if exist .\%para_ab_example_name%.cpp (
        set para_ab_example_suffix=cpp
    ) else if exist .\wm_src\weware_app\%para_ab_example_name%.c (
        set para_ab_example_name=wm_src/weware_app/%para_ab_example_name%
        set para_ab_example_suffix=c
    )

    if "%para_ab_example_suffix%"=="" (
        echo example %para_ab_example_name% not exist!
        exit /B 1
    )

    echo build %para_ab_example_name%.%para_ab_example_suffix%

    (
        setlocal enabledelayedexpansion
        for /f "delims=" %%i in (.\CMakeLists.txt.in) do (
            set s=%%i
            @REM set s=!s:customer_app=%para_ab_example_name%_app!
            set s=!s:hello_world.c=%para_ab_example_name%.%para_ab_example_suffix%!
            echo !s!
        )
        @REM endlocal
    )>.\CMakeLists.txt
    
    call :_build
    goto :EOF


:_build
    call .\tools\core_launch.bat
    cd %folder_ab_build_out%\appimage_debug
    cmake ..\.. -G Ninja
    ninja
    cd ..\..

    if exist ..\kernel\ (
        xcopy /Q /Y .\out\appimage_debug\hex\customer_app.bin  ..\kernel\%module_version%
    )

    goto :EOF


:clean
    echo clean 
    if exist %folder_ab_build_out% (
        echo rmdir %folder_ab_build_out%
        rmdir /s /q %folder_ab_build_out%
    )
    goto :EOF

:download
    call download.bat -app
    goto :EOF
