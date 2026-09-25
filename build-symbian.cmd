@echo off
rem Builds the phone packages from the command line (Qt Creator does the same through its
rem Symbian kit). Qt for Symbian supports in-source builds only, so this runs in the
rem project directory; the generated files are listed in .gitignore.
rem
rem   build-symbian.cmd            release ARMv5 build, then the self-signed package:
rem                                  Symbigram_<ver>.sis   installs on Belle and Symbian^3 / Anna
rem                                (bumps the patch version first). The app still uses Pigler for
rem                                Belle status-bar notifications IF Pigler is installed separately;
rem                                it is NOT bundled here (to avoid clashing Pigler versions across
rem                                installers). Pigler.sis is kept in the repo for manual install.
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

rem One self-signed package (installs on Belle and Anna). Pigler is NOT embedded - install it
rem separately from Pigler.sis if you want the Belle status-bar notifications. The No-Anna Smart
rem Installer wrapper is not built (its online Qt Quick Components resource no longer exists).
call createpackage.bat Symbigram_template.pkg release-armv5
if errorlevel 1 exit /b 1

rem The only package left behind carries the version in its name; the unsigned intermediate goes.
if exist Symbigram.sis ( move /y Symbigram.sis Symbigram_%VER%.sis >nul & echo built Symbigram_%VER%.sis )
del /q Symbigram_unsigned.sis 2>nul
