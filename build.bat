@echo off
setlocal
echo [INFO] Setting up MSVC environment...
call "C:\Game\BeiDou-Server\tools\msvc\setup_x86.bat"

cd /d "%~dp0ezorsia"
echo [INFO] Compiling ijl15.dll...
cl /source-charset:utf-8 /execution-charset:gbk /O2 /MD /EHsc /std:c++17 /D WIN32 /D NDEBUG /D _WINDOWS /D _USRDLL /D EZORSIA_EXPORTS /D _CRT_SECURE_NO_WARNINGS /I. /I..\detours *.cpp /link /DLL /MACHINE:X86 /LIBPATH:..\detours detours.lib imm32.lib ws2_32.lib user32.lib gdi32.lib advapi32.lib ole32.lib oleaut32.lib /OUT:ijl15.dll

if %ERRORLEVEL% equ 0 (
    echo [SUCCESS] ijl15.dll compiled successfully!
    echo Output: %~dp0ezorsia\ijl15.dll
    
    if exist "C:\Game\BeiDou-Client" (
        copy /y "%~dp0ezorsia\ijl15.dll" "C:\Game\BeiDou-Client\ijl15.dll" >nul
        echo [DEPLOY] Deployed ijl15.dll to C:\Game\BeiDou-Client\ijl15.dll
    )
) else (
    echo [ERROR] Compilation failed.
)
