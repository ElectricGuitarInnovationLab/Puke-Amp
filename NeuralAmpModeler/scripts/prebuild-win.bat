@echo off
REM - CALL "$(SolutionDir)scripts\prebuild-win.bat" "$(TargetExt)" "$(BINARY_NAME)" "$(Platform)" "$(TargetPath)" "$(OutDir)"
REM set FORMAT=%1
REM set NAME=%2
REM set PLATFORM=%3
REM set BUILT_BINARY=%4
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0write-version-resource.ps1" -OutputDirectory "%~6"
if errorlevel 1 exit /b 1
exit /b 0
