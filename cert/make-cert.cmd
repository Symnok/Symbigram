@echo off
call D:\QtSDK\Symbian\SDKs\SymbianSR1Qt474\env.bat
cd /d %~dp0
makekeys -cert -expdays 3650 -password symbigram -len 2048 -dname "CN=Symbigram OU=Development O=Symbigram C=IL" symbigram.key symbigram.cer
