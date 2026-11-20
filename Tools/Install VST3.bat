@echo off
setlocal
rem Copies Open Mastering Studio.vst3 into the system VST3 folder (needs administrator rights).
set "SRC=%~dp0VST3\Open Mastering Studio.vst3"
set "DST=%CommonProgramFiles%\VST3"

net session >nul 2>&1
if %errorlevel% neq 0 (
    echo Requesting administrator permission...
    powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
    exit /b
)

if not exist "%SRC%" (
    echo Could not find "%SRC%". Extract the whole zip first, then run this again.
    pause
    exit /b 1
)

if not exist "%DST%" mkdir "%DST%"
rem Versions before 1.8.0 were called "Mastering Studio". It is the same plugin (same ID, so your
rem sessions load it), so remove the old copy: two copies of one plugin confuse DAWs.
if exist "%DST%\Mastering Studio.vst3" (
    attrib -r "%DST%\Mastering Studio.vst3" >nul 2>&1
    rmdir /s /q "%DST%\Mastering Studio.vst3"
    echo Removed the old "Mastering Studio.vst3" ^(now called Open Mastering Studio^).
)
if exist "%DST%\Open Mastering Studio.vst3" (
    attrib -r "%DST%\Open Mastering Studio.vst3" >nul 2>&1
    rmdir /s /q "%DST%\Open Mastering Studio.vst3"
)
xcopy "%SRC%" "%DST%\Open Mastering Studio.vst3\" /e /i /q /y >nul
if %errorlevel% neq 0 (
    echo Copy failed.
    pause
    exit /b 1
)
rem Let Explorer show the plugin's folder icon (desktop.ini needs a read-only/system folder)
attrib +r "%DST%\Open Mastering Studio.vst3" >nul 2>&1
attrib +h +s "%DST%\Open Mastering Studio.vst3\desktop.ini" >nul 2>&1
echo.
echo Installed to "%DST%\Open Mastering Studio.vst3"
echo Rescan plugins in your DAW to find Open Mastering Studio.
echo.
pause
