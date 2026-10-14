@echo off
setlocal

@REM Pick the entry file from WM_BUILD_PURPOSE in CMakeLists.txt.in
@REM   WM_PROD_APP -> weware_main
@REM   WM_UI_APP   -> wm_app_main
@REM   WM_LIB      -> no application image to build
set "purpose_line="
for /f "tokens=1 delims=#" %%L in ('findstr /b /c:"set(WM_BUILD_PURPOSE " CMakeLists.txt.in') do set "purpose_line=%%L"

if not defined purpose_line (
    echo WM_BUILD_PURPOSE not found in CMakeLists.txt.in
    exit /b 1
)

set "app_target="
if not "%purpose_line:WM_PROD_APP=%"=="%purpose_line%" set "app_target=weware_main"
if not "%purpose_line:WM_UI_APP=%"=="%purpose_line%" set "app_target=wm_app_main"

if not defined app_target (
    echo WM_BUILD_PURPOSE is WM_LIB or unknown - no application image to build
    exit /b 1
)

echo WM_BUILD_PURPOSE entry: %app_target%
.\build.bat -bv GW1NS -fs 4 -t %app_target%
