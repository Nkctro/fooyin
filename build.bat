@echo off
setlocal EnableExtensions EnableDelayedExpansion

:: remember original code page (optional)
for /f "tokens=2 delims=: " %%code_page in ('chcp') do set "ORIG_CP=%%code_page"
set "ORIG_CP=%ORIG_CP: =%"
if not "%ORIG_CP%"=="65001" (
    chcp 65001 >nul
    set "RESET_CP=1"
)

echo ==============================================
echo Select Language / 选择语言
echo   [1] 中文
echo   [2] English
call :AskChoiceSimple LANG_CHOICE "Enter choice / 选择编号 [Enter=2 English]: " 2
if "%LANG_CHOICE%"=="1" (
    set "LANG=CN"
) else (
    set "LANG=EN"
)
echo.

:: Step 1. Detect CMake
:DETECT_CMAKE
where cmake >nul 2>nul
if %errorlevel%==0 (
    for /f "delims=" %%cmake_path in ('where cmake') do set "CMAKE_EXE=%%cmake_path"
    if "%LANG%"=="CN" (
        echo [?] 已检测到 CMake: "%CMAKE_EXE%"
    ) else (
        echo [?] Found CMake: "%CMAKE_EXE%"
    )
) else (
    if "%LANG%"=="CN" (
        echo [!] 未检测到 CMake, 请先安装 https://cmake.org/download/
        set /p CMAKE_EXE=请输入 cmake.exe 路径: 
    ) else (
        echo [!] CMake not found. Download: https://cmake.org/download/
        set /p CMAKE_EXE=Enter cmake.exe path: 
    )
)
set "CMAKE_EXE=%CMAKE_EXE:\"=%"
set "CMAKE_EXE=%CMAKE_EXE:'=%"
if not exist "%CMAKE_EXE%" (
    if "%LANG%"=="CN" (
        echo [!] 路径无效, 请重新输入。
    ) else (
        echo [!] Invalid path, please try again.
    )
    goto DETECT_CMAKE
)
echo.

:: Step 2. Detect Qt
if defined QT_ROOT_DIR (
    if exist "%QT_ROOT_DIR%\bin\qmake.exe" (
        if "%LANG%"=="CN" (
            echo [?] 已检测到 Qt: "%QT_ROOT_DIR%"
        ) else (
            echo [?] Detected Qt at "%QT_ROOT_DIR%"
        )
    ) else (
        set "QT_ROOT_DIR="
    )
)
if not defined QT_ROOT_DIR (
    if "%LANG%"=="CN" (
        echo [!] 未检测到 Qt, 请输入路径或下载 https://www.qt.io/download/
        set /p QT_ROOT_DIR=Qt 路径: 
    ) else (
        echo [!] Qt not detected. Input path or download: https://www.qt.io/download/
        set /p QT_ROOT_DIR=Qt path: 
    )
)
set "QT_ROOT_DIR=%QT_ROOT_DIR:\"=%"
set "QT_ROOT_DIR=%QT_ROOT_DIR:'=%"
echo.

:: Step 3. Decide whether to use vcpkg
if "%LANG%"=="CN" (
    echo 是否使用 vcpkg?
    echo   [1] 是
    echo   [2] 否
    call :AskChoiceSimple VCPKG_CHOICE "请选择 [回车=2 否]: " 2
) else (
    echo Use vcpkg?
    echo   [1] Yes
    echo   [2] No
    call :AskChoiceSimple VCPKG_CHOICE "Enter choice [Enter=2 No]: " 2
)
set "VCPKG=0"
if "%VCPKG_CHOICE%"=="1" set "VCPKG=1"
if "%VCPKG%"=="1" (
    if defined VCPKG_ROOT (
        if exist "%VCPKG_ROOT%\vcpkg.exe" (
            if "%LANG%"=="CN" (
                echo [?] 已检测到 vcpkg: "%VCPKG_ROOT%"
            ) else (
                echo [?] Detected vcpkg at "%VCPKG_ROOT%"
            )
        ) else (
            set "VCPKG_ROOT="
        )
    )
    if not defined VCPKG_ROOT (
        if "%LANG%"=="CN" (
            echo [!] 未检测到 vcpkg, 请输入路径或下载 https://vcpkg.io/
            set /p VCPKG_ROOT=vcpkg 路径: 
        ) else (
            echo [!] vcpkg not found. Input path or download: https://vcpkg.io/
            set /p VCPKG_ROOT=vcpkg path: 
        )
    )
    set "VCPKG_ROOT=%VCPKG_ROOT:\"=%"
    set "VCPKG_ROOT=%VCPKG_ROOT:'=%"
)
echo.

:: Step 4. Detect Visual Studio command prompt
set "VS_CMD=C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"
if not exist "%VS_CMD%" (
    if "%LANG%"=="CN" (
        echo [!] 未检测到 Visual Studio 命令提示, 请输入 VsDevCmd.bat 路径:
        echo     https://visualstudio.microsoft.com/zh-hans/downloads/
        set /p VS_CMD=VsDevCmd.bat 路径: 
    ) else (
        echo [!] Visual Studio command prompt not found. Provide VsDevCmd.bat path or install VS:
        echo     https://visualstudio.microsoft.com/downloads/
        set /p VS_CMD=VsDevCmd.bat path: 
    )
)
set "VS_CMD=%VS_CMD:\"=%"
set "VS_CMD=%VS_CMD:'=%"
if not exist "%VS_CMD%" (
    if "%LANG%"=="CN" (
        echo [警告] 找不到 VsDevCmd.bat, 生成脚本时将直接使用该路径。
    ) else (
        echo [WARN] VsDevCmd.bat not found; generated script will use the provided path.
    )
)
echo.

:: Step 5. Select build mode
if "%LANG%"=="CN" (
    echo 请选择构建模式:
    echo   [1] 调试模式 (Debug)
    echo   [2] 发布模式 (Release)
    call :AskChoiceSimple MODE_CHOICE "请选择 [回车=2 发布]: " 2
) else (
    echo Select build type:
    echo   [1] Debug
    echo   [2] Release
    call :AskChoiceSimple MODE_CHOICE "Enter choice [Enter=2 Release]: " 2
)
if "%MODE_CHOICE%"=="1" (
    set "MODE=Debug"
) else (
    set "MODE=Release"
)
echo.

:: Summary
if "%LANG%"=="CN" (
    set "VCPKG_LABEL=否"
    if "%VCPKG%"=="1" set "VCPKG_LABEL=是"
    if "%MODE%"=="Debug" (
        echo [信息] 已选择: 语言=中文, vcpkg=!VCPKG_LABEL!, 模式=调试。
    ) else (
        echo [信息] 已选择: 语言=中文, vcpkg=!VCPKG_LABEL!, 模式=发布。
    )
) else (
    set "VCPKG_LABEL=No"
    if "%VCPKG%"=="1" set "VCPKG_LABEL=Yes"
    echo [INFO] Selected: language=English, vcpkg=!VCPKG_LABEL!, mode=!MODE!.
)
echo.

:: Step 6. Optional Release rebuild script
if "%LANG%"=="CN" (
    call :AskChoiceSimple REBUILD_CHOICE "是否生成 Release 重编译脚本? [回车=默认生成]: " 1
) else (
    call :AskChoiceSimple REBUILD_CHOICE "Generate Release rebuild script? [Enter=1 Yes]: " 1
)
if "%REBUILD_CHOICE%"=="1" (
    call :GenerateRebuildScript
)
echo.

if defined RESET_CP chcp %ORIG_CP% >nul
endlocal
exit /b

:AskChoiceSimple
setlocal EnableExtensions EnableDelayedExpansion
set "VAR_NAME=%~1"
set "PROMPT=%~2"
set "DEFAULT=%~3"
:CHOICE_LOOP
set "ANSWER="
set /p ANSWER=%PROMPT%
if not defined ANSWER set "ANSWER=%DEFAULT%"
set "ANSWER=%ANSWER: =%"
set "ANSWER=%ANSWER:[=%"
set "ANSWER=%ANSWER:]=%"
set "ANSWER=%ANSWER:(=%"
set "ANSWER=%ANSWER:)=%"
set "ANSWER=%ANSWER:"=%"
set "ANSWER=%ANSWER:'=%"
if not defined ANSWER set "ANSWER=%DEFAULT%"
set "ANSWER=%ANSWER:~0,1%"
if /I "%ANSWER%"=="Y" set "ANSWER=1"
if /I "%ANSWER%"=="N" set "ANSWER=2"
if "%ANSWER%"=="1" goto CHOICE_OK
if "%ANSWER%"=="2" goto CHOICE_OK
if defined LANG if "%LANG%"=="CN" (
    echo [提示] 请输入 1 或 2。
) else (
    echo [INFO] Please enter 1 or 2.
)
if not defined LANG (
    echo Invalid choice, please enter 1 or 2.
)
goto CHOICE_LOOP
:CHOICE_OK
endlocal & set "%VAR_NAME%=%ANSWER%"
exit /b

:GenerateRebuildScript
setlocal EnableExtensions EnableDelayedExpansion
set "REBUILD_PRESET=release"
set "REBUILD_SCRIPT=rebuild_release.bat"
if "%VCPKG%"=="1" (
    set "REBUILD_PRESET=release-vcpkg"
    set "REBUILD_SCRIPT=rebuild_release-vcpkg.bat"
)
set "CMAKE_CMD=%CMAKE_EXE%"
set "VS_CMD_PATH=%VS_CMD%"
:: Detect system architecture for VS command prompt
set "ARCH=amd64"
if /I "%PROCESSOR_ARCHITECTURE%"=="ARM64" set "ARCH=arm64"
if "%LANG%"=="CN" (
    echo 正在生成 Release 重编译脚本 "!REBUILD_SCRIPT!"...
) else (
    echo Generating Release rebuild script "!REBUILD_SCRIPT!"...
)
(
    echo @echo off
    echo setlocal EnableExtensions EnableDelayedExpansion
    echo call "!VS_CMD_PATH!" -arch=!ARCH! -host_arch=!ARCH!
    echo if errorlevel 1 ^(
    if "%LANG%"=="CN" (
        echo ^    echo [警告] 无法初始化 Visual Studio 环境。
    ) else (
        echo ^    echo [WARN] Failed to initialize Visual Studio environment.
    )
    echo ^)
    echo pause ^>nul
    echo exit /b 1
    echo chcp 65001 ^>nul
    echo echo.
    if "%LANG%"=="CN" (
        echo echo [信息] 正在配置 Release 预设...
    ) else (
        echo echo [INFO] Configuring Release preset...
    )
    echo "%CMAKE_CMD%" --preset %REBUILD_PRESET% ^> %REBUILD_PRESET%_configure.log 2^>^&1
    echo if errorlevel 1 ^(
    if "%LANG%"=="CN" (
        echo ^    echo [警告] 配置失败, 请检查 %REBUILD_PRESET%_configure.log
    ) else (
        echo ^    echo [WARN] Configure failed, see %REBUILD_PRESET%_configure.log
    )
    echo ^    type %REBUILD_PRESET%_configure.log
    echo ^)
    echo pause ^>nul
    echo exit /b 1
    echo echo.
    if "%LANG%"=="CN" (
        echo echo [信息] 正在清理并重编译 Release...
    ) else (
        echo echo [INFO] Cleaning and rebuilding Release...
    )
    echo "%CMAKE_CMD%" --build --preset %REBUILD_PRESET% --clean-first ^> %REBUILD_PRESET%_build.log 2^>^&1
    echo if errorlevel 1 ^(
    if "%LANG%"=="CN" (
        echo ^    echo [警告] 编译失败, 请检查 %REBUILD_PRESET%_build.log
    ) else (
        echo ^    echo [WARN] Build failed, see %REBUILD_PRESET%_build.log
    )
    echo ^    type %REBUILD_PRESET%_build.log
    echo ^)
    echo pause ^>nul
    echo exit /b 1
    if "%LANG%"=="CN" (
        echo echo [信息] Release 重编译完成。
    ) else (
        echo echo [INFO] Release rebuild complete.
    )
    echo call :SilentPause
    echo endlocal
    echo goto :eof
    echo :SilentPause
    echo pause ^>nul
) >> "!REBUILD_SCRIPT!"

if "%LANG%"=="CN" (
    echo [?] 已创建 "!REBUILD_SCRIPT!"，可直接运行以重编译 Release.
) else (
    echo [?] Created "!REBUILD_SCRIPT!". Run it to rebuild Release.
)
endlocal
exit /b
