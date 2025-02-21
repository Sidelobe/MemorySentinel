@echo off

if not exist "build\vs2017-windows" mkdir build\vs2022-x64-windows
cd build\vs2022-x64-windows

cmake -G "Visual Studio 17 2022" -DCMAKE_GENERATOR_PLATFORM=x64 ..\..\..
if ERRORLEVEL 1 exit /b %ERRORLEVEL%

cd ..
