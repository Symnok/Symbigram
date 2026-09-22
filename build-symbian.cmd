@echo off
rem Builds the phone packages from the command line (Qt Creator does the same through its
rem Symbian kit). Qt for Symbian supports in-source builds only, so this runs in the
rem project directory; the generated files are listed in .gitignore.
rem
rem   build-symbian.cmd            release ARMv5 build, then both packages:
rem                                  Symbigram_<ver>.sis            self-signed, Symbian Belle
rem                                  Symbigram_installer_<ver>.sis  Smart Installer wrapper,
rem                                                                 Symbian^3 / Anna
rem                                after bumping the patch version, so Symbigram.pro
rem                                always names the newest package
rem   build-symbian.cmd clean      removes the build output
setlocal
call D:\QtSDK\Symbian\SDKs\SymbianSR1Qt474\env.bat
cd /d %~dp0

if "%1"=="clean" (
    call sbs -c arm.v5.urel.gcce4_4_1 clean
    goto :eof
)

rem Bump first: the version in Symbigram.pro is then the one being built, and each build
rem is higher than the last (Symbian refuses to replace an app with an equal version).
python bump-version.py
if errorlevel 1 exit /b 1
for /f "delims=" %%v in ('python bump-version.py --print') do set VER=%%v
echo === building Symbigram %VER%

qmake Symbigram.pro -spec symbian-sbsv2 CONFIG+=release
if errorlevel 1 exit /b 1
call sbs -c arm.v5.urel.gcce4_4_1
if errorlevel 1 exit /b 1

rem Both packages wrap the same binary: the self-signed one for Belle, and the Smart
rem Installer one for Anna, which fetches Qt Quick Components before installing the app.
call createpackage.bat Symbigram_template.pkg release-armv5
if errorlevel 1 exit /b 1
call createpackage.bat Symbigram_installer.pkg release-armv5
if errorlevel 1 exit /b 1

rem The only packages left behind carry the version in their names; the unversioned and
rem unsigned intermediates go.
if exist Symbigram.sis ( move /y Symbigram.sis Symbigram_%VER%.sis >nul & echo built Symbigram_%VER%.sis )
if exist Symbigram_installer.sis ( move /y Symbigram_installer.sis Symbigram_installer_%VER%.sis >nul & echo built Symbigram_installer_%VER%.sis )
del /q Symbigram_unsigned.sis Symbigram_installer_unsigned.sis 2>nul
