@echo off

if not exist "build\vs2017-windows" mkdir build\vs2017-windows
cd build\vs2017-windows

cmake -G "Visual Studio 16 2019" -DCMAKE_GENERATOR_PLATFORM=x64 ..\..\..
if ERRORLEVEL 1 exit /b %ERRORLEVEL%

cd ..