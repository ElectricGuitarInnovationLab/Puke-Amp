@echo off
setlocal EnableExtensions

for %%I in ("%~dp0..") do set "PROJECT_DIR=%%~fI"
set "PLUGIN_NAME=Puke Amp.vst3"
set "BUNDLE_PATH=%PROJECT_DIR%\build-win\%PLUGIN_NAME%"
set "PLUGIN_BINARY=%BUNDLE_PATH%\Contents\x86_64-win\Puke Amp.vst3"
set "OUTPUT_DIR=%PROJECT_DIR%\build-vst3\windows"
set "OUTPUT_PATH=%OUTPUT_DIR%\%PLUGIN_NAME%"
set "INSTALLER_SCRIPT=%PROJECT_DIR%\installer\PukeAmp-VST3.iss"

where msbuild >nul 2>nul
if errorlevel 1 (
  echo error: msbuild was not found. Run this from a Visual Studio Developer Command Prompt.
  exit /b 1
)

if not exist "%PROJECT_DIR%\..\iPlug2\Dependencies\IPlug\VST3_SDK" (
  echo error: the VST3 SDK is missing.
  echo Run iPlug2\Dependencies\IPlug\download-iplug-sdks.sh first.
  exit /b 1
)

set "BUILD_TARGET=NeuralAmpModeler-vst3"
set "EXPORT_PROPERTIES="
if /i "%~1"=="--bundle-only" (
  set "BUILD_TARGET=NeuralAmpModeler-vst3:Rebuild"
  set "EXPORT_PROPERTIES=/p:VST3_32_PATH= /p:VST3_64_PATH="
)
msbuild "%PROJECT_DIR%\NeuralAmpModeler.sln" /t:%BUILD_TARGET% /p:Configuration=Release /p:Platform=x64 %EXPORT_PROPERTIES% /m /nologo
if errorlevel 1 exit /b 1

if not exist "%BUNDLE_PATH%" (
  echo error: build succeeded but "%BUNDLE_PATH%" was not created.
  exit /b 1
)

REM Exporters stage all resources before signing; preserve the default release path.
if /i "%~1"=="--bundle-only" exit /b 0

if defined WINDOWS_SIGNTOOL_PATH (
  if not defined WINDOWS_CERTIFICATE_PATH (
    echo error: WINDOWS_CERTIFICATE_PATH is required when WINDOWS_SIGNTOOL_PATH is set.
    exit /b 1
  )
  if not defined WINDOWS_CERTIFICATE_PASSWORD (
    echo error: WINDOWS_CERTIFICATE_PASSWORD is required when WINDOWS_SIGNTOOL_PATH is set.
    exit /b 1
  )

  if not exist "%PLUGIN_BINARY%" (
    echo error: VST3 binary "%PLUGIN_BINARY%" was not found.
    exit /b 1
  )

  "%WINDOWS_SIGNTOOL_PATH%" sign /f "%WINDOWS_CERTIFICATE_PATH%" /p "%WINDOWS_CERTIFICATE_PASSWORD%" /fd SHA256 /tr https://timestamp.digicert.com /td SHA256 "%PLUGIN_BINARY%"
  if errorlevel 1 exit /b 1
  "%WINDOWS_SIGNTOOL_PATH%" verify /pa /v "%PLUGIN_BINARY%"
  if errorlevel 1 exit /b 1
)

if exist "%OUTPUT_PATH%" rmdir /s /q "%OUTPUT_PATH%"
if not exist "%OUTPUT_DIR%" mkdir "%OUTPUT_DIR%"
xcopy /E /I /Y "%BUNDLE_PATH%" "%OUTPUT_PATH%\" >nul
if errorlevel 1 exit /b 1

for /f "tokens=3" %%V in ('findstr /b /c:"#define PLUG_VERSION_STR " "%PROJECT_DIR%\config.h"') do set "PLUGIN_VERSION=%%~V"
if not defined PLUGIN_VERSION (
  echo error: PLUG_VERSION_STR was not found in "%PROJECT_DIR%\config.h".
  exit /b 1
)

set "ARTIFACT_SUFFIX=-Unsigned"
if defined WINDOWS_SIGNTOOL_PATH set "ARTIFACT_SUFFIX="
set "ARCHIVE_PATH=%OUTPUT_DIR%\Puke-Amp-%PLUGIN_VERSION%-Windows%ARTIFACT_SUFFIX%.zip"
set "INSTALLER_PATH=%OUTPUT_DIR%\Puke-Amp-%PLUGIN_VERSION%-Windows-Setup%ARTIFACT_SUFFIX%.exe"

powershell -NoProfile -Command "Compress-Archive -LiteralPath '%OUTPUT_PATH%' -DestinationPath '%ARCHIVE_PATH%' -Force"
if errorlevel 1 exit /b 1

set "ISCC=%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"
if not exist "%ISCC%" set "ISCC=%ProgramFiles%\Inno Setup 6\ISCC.exe"
if not exist "%ISCC%" (
  for /f "delims=" %%I in ('where iscc 2^>nul') do if not defined ISCC_FOUND set "ISCC=%%I"
)
if not exist "%ISCC%" (
  echo error: Inno Setup 6 was not found. Install it from https://jrsoftware.org/isinfo.php
  exit /b 1
)

if exist "%INSTALLER_PATH%" del /q "%INSTALLER_PATH%"
"%ISCC%" /Qp "/DAppVersion=%PLUGIN_VERSION%" "/DArtifactSuffix=%ARTIFACT_SUFFIX%" "%INSTALLER_SCRIPT%"
if errorlevel 1 exit /b 1
if not exist "%INSTALLER_PATH%" (
  echo error: installer compilation succeeded but "%INSTALLER_PATH%" was not created.
  exit /b 1
)

if defined WINDOWS_SIGNTOOL_PATH (
  "%WINDOWS_SIGNTOOL_PATH%" sign /f "%WINDOWS_CERTIFICATE_PATH%" /p "%WINDOWS_CERTIFICATE_PASSWORD%" /fd SHA256 /tr https://timestamp.digicert.com /td SHA256 "%INSTALLER_PATH%"
  if errorlevel 1 exit /b 1
  "%WINDOWS_SIGNTOOL_PATH%" verify /pa /v "%INSTALLER_PATH%"
  if errorlevel 1 exit /b 1
)

echo Built "%OUTPUT_PATH%"
echo Packaged "%ARCHIVE_PATH%"
echo Packaged "%INSTALLER_PATH%"
exit /b 0
