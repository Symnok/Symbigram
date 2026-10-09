@echo off
rem Builds one Symbigram language pack.   Usage: build-pack.cmd <lang> <folder>
rem    e.g.  build-pack.cmd he hebrew
rem
rem   1. lupdate   - re-reads every string from src/ and qml/ into the pack's .ts
rem   2. fill.py   - puts the pack's translations into that .ts
rem   3. lrelease  - compiles the .qm
rem   4. createpackage - wraps the .qm in a self-signed, data-only .sis
rem
rem Re-run after changing application strings. Anything the pack does not cover stays English.
setlocal
set LANG=%1
set FOLDER=%2
if "%LANG%"=="" echo usage: build-pack.cmd ^<lang^> ^<folder^> & exit /b 1
set ADDONS=%~dp0
set ROOT=%ADDONS%..
set QTBIN=D:\QtSDK\Desktop\Qt\4.7.4\mingw\bin
set PACK=%ADDONS%%FOLDER%

pushd "%ROOT%"
"%QTBIN%\lupdate.exe" -silent -extensions qml,cpp,h -no-obsolete src qml -ts "%PACK%\symbigram_%LANG%.ts" || goto :fail
python "%ADDONS%fill.py" %LANG% "%PACK%" || goto :fail
"%QTBIN%\lrelease.exe" "%PACK%\symbigram_%LANG%.ts" || goto :fail
popd

call D:\QtSDK\Symbian\SDKs\SymbianSR1Qt474\env.bat
pushd "%PACK%"
call createpackage.bat symbigram_%LANG%.pkg || goto :fail
popd

echo.
echo Built the %FOLDER% language pack.
goto :eof

:fail
echo BUILD FAILED
exit /b 1
