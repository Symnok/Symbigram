@echo off
rem Builds the phone packages from the command line (Qt Creator does the same through its
rem Symbian kit). Qt for Symbian supports in-source builds only, so this runs in the
rem project directory; the generated files are listed in .gitignore.
rem
rem   build-symbian.cmd            release ARMv5 build, then two self-signed packages:
rem                                  Symbigram_Belle_<ver>.sis     Belle - also installs the
rem                                                                embedded Pigler.sis (status-bar
rem                                                                notifications)
rem                                  Symbigram_S3_Anna_<ver>.sis   Symbian^3 / Anna - no Pigler
rem                                (bumps the patch version first; the same binary is in both)
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

rem Two packages from the same qmake-generated template (do not hand-edit Symbigram_template.pkg;
rem qmake regenerates it): the Belle one additionally embeds Pigler.sis (package UID 0x20292b69)
rem so installing it also installs Pigler; the S^3/Anna one is the app alone. The No-Anna Smart
rem Installer wrapper is not built (its online Qt Quick Components resource no longer exists).
copy /y Symbigram_template.pkg Symbigram_S3_Anna_template.pkg >nul
copy /y Symbigram_template.pkg Symbigram_Belle_template.pkg >nul
>>Symbigram_Belle_template.pkg echo @"Pigler.sis",(0x20292b69)

call createpackage.bat -n Symbigram_S3_Anna_%VER%.sis Symbigram_S3_Anna_template.pkg release-armv5
if errorlevel 1 exit /b 1
call createpackage.bat -n Symbigram_Belle_%VER%.sis Symbigram_Belle_template.pkg release-armv5
if errorlevel 1 exit /b 1
echo built Symbigram_S3_Anna_%VER%.sis
echo built Symbigram_Belle_%VER%.sis

rem Clean the temporary per-package templates and unsigned intermediates.
del /q Symbigram_S3_Anna_template.pkg Symbigram_Belle_template.pkg 2>nul
del /q Symbigram_S3_Anna_release-armv5.pkg Symbigram_Belle_release-armv5.pkg 2>nul
del /q Symbigram_S3_Anna_%VER%_unsigned.sis Symbigram_Belle_%VER%_unsigned.sis 2>nul
del /q Symbigram_unsigned.sis 2>nul
