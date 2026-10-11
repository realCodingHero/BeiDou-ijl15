@echo off
rem Double-click: build + test the quest helper UI and deploy ONLY to C:\Game\BeiDou-Client-research.
rem The full log is written to out\quest-helper-research-build.log.
pwsh -NoProfile -ExecutionPolicy Bypass -File "%~dp0build-quest-helper-research.ps1"
echo.
pause
