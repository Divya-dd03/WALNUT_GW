@echo off

set folder_dw_current=%cd%

set folder_out_bin=%folder_dw_current%\out\

set para_dw_flash_size=[]
set module_version=GW1NS
set file_app_build_config=%folder_out_bin%\bcfg
set bootrom_version=2022.11.06

if exist %file_app_build_config% (
    for /f "tokens=1,2 delims='" %%a in (%file_app_build_config%) do (
        if "%%a"=="flash=" (
            set para_dw_flash_size=%%b
        ) 
        if "%%a"=="module_version=" (
            set module_version=%%b
        ) 
        if "%%a"=="flash=" (
            set para_dw_flash_size=%%b
        )
    )
)
echo module_version: %module_version%
echo module_flash_size: %para_dw_flash_size%

if "%para_dw_flash_size%"=="[]" (
    echo %para_dw_flash_size%
    echo load flash size error
    exit /B 1
)
set folder_dw_tool=%folder_dw_current%\..\..\Tools\aboot\
set file_dw_download_modify_script=%folder_dw_current%\tools\download_modify.py
set cmd_dw_7z=%folder_dw_current%\prebuilts\win32\bin\7z.exe
set cmd_dw_python=%folder_dw_current%\prebuilts\win32\python3\python3.exe

set para_dw_target=app
set cmd_dw_del_folder=rmdir /s /q
set cmd_dw_del_file=del /Q
set cmd_dw_file_copy=xcopy /Q /y
set cmd_dw_folder_copy=RoboCopy /MT[:8] /E

set file_dw_release=[]
set para_dw_release_only=n

@REM parse parameters
goto para_dw_parse
:para_dw_parse
if not "%1"=="" (
    if "%1"=="-p" (
        set file_dw_release=%2
        shift /0
    ) else if "%1"=="-t" (
        set folder_dw_tool=%2
        shift /0
    ) else if "%1"=="-b" (
        set folder_dw_bin=%2
        shift /0
    ) else if "%1"=="-app" (
        set para_dw_target=app
    )else if "%1"=="-rf" (
        set para_dw_target=rf
    ) else if "%1"=="-all" (
        set para_dw_target=norf
    )else if "%1"=="-wf" (
        set para_dw_target=nofs_norf
    )else if "%1"=="-ALL" (
        set para_dw_target=all
    ) else if "%1"=="-r" (
        set para_dw_release_only=y
    ) else (
        echo unknown para %1
        exit /B 1
    )

    shift /0
    goto para_dw_parse
)


@REM check tool
set cmd_dw_release=%folder_dw_tool%\arelease.exe
set cmd_dw_download=%folder_dw_tool%\adownload.exe
if not exist %cmd_dw_release% (
    echo release tool not exist
    exit /B 1
)
if not exist %cmd_dw_download% (
    echo download tool not exist
    exit /B 1
)

if not "%file_dw_release%"=="[]" (
    cd %folder_dw_tool%
    goto download
) 

@REM platform type
set para_dw_platform=[]
set para_dw_variants=[]

if %module_version%==GW1NS (
    set bootrom_version=2022.11.06_ARB
    if "%para_dw_flash_size%" == "2" (
        set module_version=%module_version%-2
        set para_dw_platform=ASR1605_EVB
        set para_dw_variants=ASR1605_OPEN_02MB_CP2_O51
    ) else if "%para_dw_flash_size%" == "4" (
        set module_version=%module_version%-4
        set para_dw_variants=ASR1605_OPEN_TRACKER_04MB
        set para_dw_platform=ASR1605_EVB
    ) else (
        echo "GW1NS flash size only support 4M and 2M"
        exit /B 1
    )
) else if %module_version%==ZX600CH (
    set module_version=%module_version%-8
    if "%para_dw_flash_size%" == "16" (
        set para_dw_platform=ASR1603_EVB
        set para_dw_variants=ASR1603_16M16M_OPEN      
    ) else if "%para_flash_size%" == "8" (
        set para_dw_platform=ASR1603_EVB
        set para_dw_variants=ASR1603_8M8M_OPEN
    ) else (
        echo "ZX600CH flash size only support 16M and 8M"
        exit /B 1
    )	
) 
set folder_dw_bin=%folder_dw_current%\..\kernel\%module_version%\

@REM load build config
set file_dw_build_config=%folder_dw_bin%\bcfg
set para_dw_release_name=[]
set para_dw_build_git_version=[]
set para_dw_build_time=[]
if exist %file_dw_build_config% (
    for /f "tokens=1,2 delims='" %%a in (%file_dw_build_config%) do (
        if "%%a"=="build_sw_version=" (
            set para_dw_release_name=%%b
        ) 
        if "%%a"=="build_git_version=" (
            set para_dw_build_git_version=%%b
        )
        if "%%a"=="build_time=" (
            set para_dw_build_time=%%b
        )
    )
)

echo tool: %folder_dw_tool%
echo bin: %folder_dw_bin%
echo target: %para_dw_target%
echo pac: %file_dw_release%

@REM check bin
set file_dw_cp_bin=%folder_dw_bin%\cp.bin
set file_dw_cp2_bin=%folder_dw_bin%\cp2.bin
set file_dw_dsp_bin=%folder_dw_bin%\dsp.bin
set file_dw_rd_bin=%folder_dw_bin%\ReliableData.bin
set file_dw_rf_bin=%folder_dw_bin%\rf.bin
set file_dw_logo_bin=%folder_dw_bin%\logo.bin
set file_dw_updater_bin=%folder_dw_bin%\updater.bin
set file_dw_boot33_bin=%folder_dw_bin%\boot33.bin
set file_dw_app_bin=%folder_dw_bin%\customer_app.bin
set file_dw_preboot_bin=%folder_dw_tool%\images\%bootrom_version%\preboot.bin
set file_dw_boot2_bin=%folder_dw_tool%\images\%bootrom_version%\boot2.bin
set file_dw_flasher_bin=%folder_dw_tool%\images\%bootrom_version%\flasher.bin
if not exist %file_dw_cp_bin% (
    echo cp.bin not exist
    exit /B 1
)
if not exist %file_dw_dsp_bin% (
    echo dsp.bin not exist
    exit /B 1
)
if not exist %file_dw_rf_bin% (
    echo rf.bin not exist
    exit /B 1
)
if not exist %file_dw_logo_bin% (
    echo logo.bin not exist
    exit /B 1
)
if not exist %file_dw_updater_bin% (
    echo updater.bin not exist
    exit /B 1
)
if not exist %file_dw_boot33_bin% (
    echo boot33.bin not exist
    exit /B 1
)
if not exist %file_dw_app_bin% (
    echo customer_app.bin not exist, copy default[no app]
    echo F | %cmd_dw_file_copy% %folder_dw_tool%\images\customer_app.bin %file_dw_app_bin%
)

@REM goto aboot tool folder
cd %folder_dw_tool%


@REM release pac all
set para_dw_release_all_name=%para_dw_release_name%_all
set folder_dw_release_all=.\%para_dw_release_all_name%\
set file_dw_release_all=.\%para_dw_release_all_name%.zip
set preboot_bin=
set flasher_bin=
set boot2_bin=
if %module_version:~0,7%==GW1NS (
    set preboot_bin=,preboot_bin=%file_dw_preboot_bin%
    set flasher_bin=,flasher_bin=%file_dw_flasher_bin%
    set boot2_bin=,boot2_bin=%file_dw_boot2_bin%

    echo arelease.exe -c . -q -g -p %para_dw_platform% -v %para_dw_variants% -i cp=%file_dw_cp_bin%,cp2=%file_dw_cp2_bin%,dsp=%file_dw_dsp_bin%,rfbin=%file_dw_rf_bin%,boot33_bin=%file_dw_boot33_bin%,logo=%file_dw_logo_bin%,updater_bin=%file_dw_updater_bin%,customer_app=%file_dw_app_bin%%preboot_bin%%flasher_bin%%boot2_bin% %file_dw_release_all%
    arelease.exe -c . -q -g -p %para_dw_platform% -v %para_dw_variants% -i cp=%file_dw_cp_bin%,cp2=%file_dw_cp2_bin%,dsp=%file_dw_dsp_bin%,rfbin=%file_dw_rf_bin%,rd=%file_dw_rd_bin%,boot33_bin=%file_dw_boot33_bin%,logo=%file_dw_logo_bin%,updater_bin=%file_dw_updater_bin%,customer_app=%file_dw_app_bin%%preboot_bin%%flasher_bin%%boot2_bin% %file_dw_release_all%
) else (
    echo arelease.exe -c . -q -g -p %para_dw_platform% -v %para_dw_variants% -i cp=%file_dw_cp_bin%,dsp=%file_dw_dsp_bin%,rfbin=%file_dw_rf_bin%,boot33_bin=%file_dw_boot33_bin%,rd=%file_dw_rd_bin%,logo=%file_dw_logo_bin%,updater_bin=%file_dw_updater_bin%,customer_app=%file_dw_app_bin% %file_dw_release_all%
    arelease.exe -c . -q -g -p %para_dw_platform% -v %para_dw_variants% -i cp=%file_dw_cp_bin%,dsp=%file_dw_dsp_bin%,rfbin=%file_dw_rf_bin%,boot33_bin=%file_dw_boot33_bin%,rd=%file_dw_rd_bin%,logo=%file_dw_logo_bin%,updater_bin=%file_dw_updater_bin%,customer_app=%file_dw_app_bin% %file_dw_release_all%
)


if not "%errorlevel%"=="0" (
    echo Release package error
    goto fail_exit
)

if exist %folder_dw_release_all% (
    %cmd_dw_del_folder% %folder_dw_release_all%
)
md %folder_dw_release_all%
%cmd_dw_7z% x -o%folder_dw_release_all% %file_dw_release_all%

@REM set para_dw_build_time=%para_dw_build_time::=%
@REM set para_dw_build_time=%para_dw_build_time:-=%
@REM set para_dw_build_time=%para_dw_build_time:,=%
@REM set para_dw_build_time=%para_dw_build_time: =0%
@REM set para_dw_release_dst=%para_dw_release_name%_%para_dw_build_git_version%_%para_dw_build_time%
set para_dw_release_dst=%para_dw_release_name%_map
set folder_dw_release_dst=.\%para_dw_release_dst%\
set file_dw_release_dst=.\%para_dw_release_dst%.zip
if "%para_dw_release_only%" == "y" (
    echo para_dw_release_dst: %para_dw_release_dst%
    echo folder_dw_release_dst: %folder_dw_release_dst%
    echo file_dw_release_dst: %file_dw_release_dst%

    if exist %file_dw_release_dst% (
        %cmd_dw_del_file% %file_dw_release_dst%
    )
    if exist %folder_dw_release_dst% (
        %cmd_dw_del_folder% %folder_dw_release_dst%
    )
    md %folder_dw_release_dst%

    echo F | %cmd_dw_file_copy% %folder_dw_bin%\bcfg %folder_dw_release_dst%\%para_dw_release_name%_bcfg
    echo F | %cmd_dw_file_copy% %folder_dw_bin%\cp.axf %folder_dw_release_dst%\%para_dw_release_name%_cp.axf
    echo F | %cmd_dw_file_copy% %folder_dw_bin%\cp.map %folder_dw_release_dst%\%para_dw_release_name%_cp.map
    echo F | %cmd_dw_file_copy% %folder_dw_bin%\cp_MDB.txt %folder_dw_release_dst%\%para_dw_release_name%_cp_MDB.txt

    @REM call :release_new %para_dw_release_name% norf n
    @REM call :release_new %para_dw_release_name% nofs_norf n
    @REM call :release_new %para_dw_release_name% rf n
    @REM call :release_new %para_dw_release_name% app n
    @REM %cmd_dw_file_copy% .\%para_dw_release_name%_norf.zip %folder_dw_release_dst%
    @REM %cmd_dw_file_copy% .\%para_dw_release_name%_nofs_norf.zip %folder_dw_release_dst%
    @REM %cmd_dw_file_copy% .\%para_dw_release_name%_rf.zip %folder_dw_release_dst%
    @REM %cmd_dw_file_copy% .\%para_dw_release_name%_app.zip %folder_dw_release_dst%
    @REM %cmd_dw_file_copy% .\%para_dw_release_name%_all.zip %folder_dw_release_dst%

    %cmd_dw_7z% a -tzip %file_dw_release_dst% %folder_dw_release_dst%\* -r

    %cmd_dw_del_folder% %folder_dw_release_dst%
    %cmd_dw_del_folder% %folder_dw_release_all%

    cd %folder_dw_current%
    exit /B 0
)

@REM generate release target
set file_dw_release=.\%para_dw_release_name%_%para_dw_target%.zip
if not "%para_dw_target%"=="all" (
    call :release_new %para_dw_release_name% %para_dw_target% n
)

%cmd_dw_del_folder% %folder_dw_release_all%

goto download
:download
    @REM check download pac
    if not exist %file_dw_release% (
        echo %file_dw_release% not exist
        goto fail_exit
    )

    @REM download
    echo start burn %file_dw_release% ......
    adownload.exe -q -r -f -u -a -s 3686400 %file_dw_release%
    if not "%errorlevel%"=="0" (
        echo Download package error
        goto fail_exit
    )

    goto success_exit

:success_exit
    cd %folder_dw_current%
    exit /B 0

:fail_exit
    cd %folder_dw_current%
    exit /B 1 

:release_new
    set para_dw_tmp_release_name=%1
    set para_dw_tmp_target=%2
    set para_dw_tmp_keep_dst_folder=%3
    set folder_dw_tmp_release_all=.\%para_dw_tmp_release_name%_all\
    set para_dw_tmp_release=%para_dw_tmp_release_name%_%para_dw_tmp_target%
    set folder_dw_tmp_release=.\%para_dw_tmp_release%\
    set file_dw_tmp_release=.\%para_dw_tmp_release%.zip

    echo ************************************************
    echo release: %para_dw_tmp_release%
    echo ************************************************

    if not exist %folder_dw_tmp_release_all% (
        echo %folder_dw_tmp_release_all% not exist
        exit /B 1
    )

    if exist %file_dw_tmp_release% (
        %cmd_dw_del_file% %file_dw_tmp_release%
    )
    if exist %folder_dw_tmp_release% (
        %cmd_dw_del_folder% %folder_dw_tmp_release%
    )
    md %folder_dw_tmp_release%

    if "%para_dw_tmp_target%"=="norf" (
        %cmd_dw_folder_copy%  %folder_dw_tmp_release_all%  %folder_dw_tmp_release%
        %cmd_dw_del_file% %folder_dw_tmp_release%\rf.bin
        %cmd_dw_del_file% %folder_dw_tmp_release%\download.json
        %cmd_dw_python% %file_dw_download_modify_script% %folder_dw_tmp_release_all%\download.json %folder_dw_tmp_release%\download.json remove flash:rf
    ) else if "%para_dw_tmp_target%"=="nofs_norf" (
        %cmd_dw_folder_copy%  %folder_dw_tmp_release_all%  %folder_dw_tmp_release%
        %cmd_dw_del_file% %folder_dw_tmp_release%\rf.bin
        %cmd_dw_del_file% %folder_dw_tmp_release%\download.json 
        %cmd_dw_python% %file_dw_download_modify_script% %folder_dw_tmp_release_all%\download.json %folder_dw_tmp_release%\download.json remove erase:fs flash:rf
    ) else if "%para_dw_tmp_target%"=="rf" (
        %cmd_dw_file_copy% %folder_dw_tmp_release_all%\preboot.img %folder_dw_tmp_release%
        %cmd_dw_file_copy% %folder_dw_tmp_release_all%\partition.bin %folder_dw_tmp_release%
        %cmd_dw_file_copy% %folder_dw_tmp_release_all%\flasher.img %folder_dw_tmp_release%
        %cmd_dw_file_copy% %folder_dw_tmp_release_all%\flashinfo.bin %folder_dw_tmp_release%
        %cmd_dw_file_copy% %folder_dw_tmp_release_all%\rf.bin %folder_dw_tmp_release%
        %cmd_dw_python% %file_dw_download_modify_script% %folder_dw_tmp_release_all%\download.json %folder_dw_tmp_release%\download.json keep flash:rf
    ) else ( 
        @REM if "%para_dw_tmp_target%"=="app" ()
        %cmd_dw_file_copy% %folder_dw_tmp_release_all%\preboot.img %folder_dw_tmp_release%
        %cmd_dw_file_copy% %folder_dw_tmp_release_all%\partition.bin %folder_dw_tmp_release%
        %cmd_dw_file_copy% %folder_dw_tmp_release_all%\flasher.img %folder_dw_tmp_release%
        %cmd_dw_file_copy% %folder_dw_tmp_release_all%\flashinfo.bin %folder_dw_tmp_release%
        %cmd_dw_file_copy% %folder_dw_tmp_release_all%\customer_app.bin %folder_dw_tmp_release%
        %cmd_dw_python% %file_dw_download_modify_script% %folder_dw_tmp_release_all%\download.json %folder_dw_tmp_release%\download.json keep flash:app
    )

    %cmd_dw_7z% a -tzip %file_dw_tmp_release% %folder_dw_tmp_release%\* -r

    if "%para_dw_tmp_keep_dst_folder%"=="n" (
        %cmd_dw_del_folder% %folder_dw_tmp_release%
    )

    goto :EOF
