@echo off
rem Builds the Symbigram Turkish language pack.
rem
rem   1. lupdate   - re-reads every string from src/ and qml/ into symbigram_tr.ts
rem   2. fill.py   - puts the Turkish text from strings_tr.py into that .ts
rem   3. lrelease  - compiles symbigram_tr.qm
rem   4. createpackage - wraps the .qm in a self-signed, data-only .sis
rem
rem Re-run it after changing application strings. Anything strings_tr.py does not cover simply
rem stays English in the pack, so it can never go stale in a way that breaks the app.
setlocal
set HERE=%~dp0
set ROOT=%HERE%..\..
set QTBIN=D:\QtSDK\Desktop\Qt\4.7.4\mingw\bin

pushd "%ROOT%"
"%QTBIN%\lupdate.exe" -silent -extensions qml,cpp,h -no-obsolete src qml -ts "%HERE%symbigram_tr.ts" || goto :fail
python "%HERE%fill.py" || goto :fail
"%QTBIN%\lrelease.exe" "%HERE%symbigram_tr.ts" || goto :fail
popd

call D:\QtSDK\Symbian\SDKs\SymbianSR1Qt474\env.bat
pushd "%HERE%"
call createpackage.bat symbigram_tr.pkg || goto :fail
popd

echo.
echo Built the Turkish language pack.
goto :eof

:fail
echo BUILD FAILED
exit /b 1
