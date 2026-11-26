@echo off
chcp 65001 >nul
setlocal enabledelayedexpansion
cd /d "%~dp0"

echo ============================================
echo   保卫萝卜 C 版 · 构建脚本
echo ============================================

where cl >nul 2>&1
if not errorlevel 1 goto build

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" set "VSWHERE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"

if not exist "%VSWHERE%" (
    echo [错误] 未找到 Visual Studio，请先安装 VS 2019/2022/2026 的“使用 C++ 的桌面开发”。
    pause
    exit /b 1
)

for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"

if not defined VSPATH (
    echo [错误] 未找到带 C++ 工具集的 Visual Studio。
    pause
    exit /b 1
)

echo [1/2] 加载编译环境: !VSPATH!
call "!VSPATH!\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if errorlevel 1 (
    echo [错误] vcvars64.bat 执行失败。
    pause
    exit /b 1
)

:build
echo [2/2] 编译 main.c ...
cl /nologo /W3 /O2 /utf-8 /D_CRT_SECURE_NO_WARNINGS main.c /Fe:carrot.exe /link user32.lib gdi32.lib msimg32.lib shell32.lib winmm.lib
if errorlevel 1 (
    echo.
    echo [失败] 编译出错，请检查上方信息。
    pause
    exit /b 1
)
del /q *.obj >nul 2>&1
echo.
echo [成功] 已生成 carrot.exe
echo 双击 carrot.exe 即可开始游戏。
echo.
pause
