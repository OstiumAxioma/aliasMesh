@echo off
rem ====================================================
rem  MaskReMesh 构建脚本 (VS2022)
rem  用法:
rem    build.bat            -> 默认 Release
rem    build.bat Debug      -> Debug 模式
rem ====================================================

set "GENERATOR=Visual Studio 17 2022"
set "BUILD_DIR=build"
set "CONFIG=Release"

if not "%~1"=="" (
    set "CONFIG=%~1"
)

if not exist "%BUILD_DIR%" (
    mkdir "%BUILD_DIR%"
)

echo.
echo === 配置项目 (%GENERATOR%, %CONFIG%) ===
cmake -S . -B "%BUILD_DIR%" -G "%GENERATOR%" -A x64
if errorlevel 1 (
    echo [错误] CMake 配置失败！
    exit /b 1
)

echo.
echo === 开始编译 (%CONFIG%) ===
cmake --build "%BUILD_DIR%" --config "%CONFIG%"
if errorlevel 1 (
    echo [错误] 构建失败！
    exit /b 1
)

echo.
echo === 构建完成 ===
echo 输出目录: %BUILD_DIR%\bin\%CONFIG%
exit /b 0