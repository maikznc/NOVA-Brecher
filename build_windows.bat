@echo off
rem NOVA BRECHER - build on Windows (VST3)
rem Needs: Visual Studio 2022 (Workload "Desktop development with C++") and CMake.
cd /d "%~dp0"

where cmake >nul 2>nul
if errorlevel 1 (
  echo CMake fehlt: https://cmake.org/download  ^(beim Installieren "Add to PATH" waehlen^)
  pause
  exit /b 1
)

cmake -B build -G "Visual Studio 17 2022" -A x64 -DBM_COPY_PLUGIN=OFF
if errorlevel 1 goto fail
cmake --build build --config Release --parallel
if errorlevel 1 goto fail

echo.
echo Fertig. Plugin liegt hier:
echo   build\NovaBrecher_artefacts\Release\VST3\NOVA BRECHER.vst3
echo Diesen Ordner nach  C:\Program Files\Common Files\VST3  kopieren.
pause
exit /b 0

:fail
echo Build fehlgeschlagen.
pause
exit /b 1
