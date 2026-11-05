@echo off
setlocal

rem All build settings live in CMakeLists.txt.  This wrapper only selects a
rem configuration and invokes CMake, so it can never drift out of sync.

cd /d "%~dp0"

set BUILD_DIR=build
set CONFIG=Release
if /I "%~1"=="debug" set CONFIG=Debug

cmake -S . -B "%BUILD_DIR%" -DCMAKE_BUILD_TYPE=%CONFIG% -DBUILD_TESTING=OFF
if errorlevel 1 goto :fail

cmake --build "%BUILD_DIR%" --config %CONFIG% --parallel
if errorlevel 1 goto :fail

echo Done: %BUILD_DIR%\bin\RMGBE.exe
exit /b 0

:fail
echo Build failed
exit /b 1
