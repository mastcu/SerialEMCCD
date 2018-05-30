Rem Builds all versions of the plugin, run as cmd /C buildAll.bat

set PATH=c:\Program Files (x86)\Microsoft Visual Studio 10.0\VC\BIN\amd64;C:\windows\Microsoft.NET\Framework64\v4.0.30319;C:\windows\Microsoft.NET\Framework64\v3.5;c:\Program Files (x86)\Microsoft Visual Studio 10.0\VC\VCPackages;c:\Program Files (x86)\Microsoft Visual Studio 10.0\Common7\IDE;c:\Program Files (x86)\Microsoft Visual Studio 10.0\Common7\Tools;C:\Program Files (x86)\HTML Help Workshop;C:\Program Files (x86)\Microsoft SDKs\Windows\v7.0A\bin\NETFX 4.0 Tools\x64;C:\Program Files (x86)\Microsoft SDKs\Windows\v7.0A\bin\x64;C:\Program Files (x86)\Microsoft SDKs\Windows\v7.0A\bin;%PATH%
set LIB=c:\Program Files (x86)\Microsoft Visual Studio 10.0\VC\LIB\amd64;c:\Program Files (x86)\Microsoft Visual Studio 10.0\VC\ATLMFC\LIB\amd64;C:\Program Files (x86)\Microsoft SDKs\Windows\v7.0A\lib\x64
set INCLUDE=c:\Program Files (x86)\Microsoft Visual Studio 10.0\VC\INCLUDE;c:\Program Files (x86)\Microsoft Visual Studio 10.0\VC\ATLMFC\INCLUDE;C:\Program Files (x86)\Microsoft SDKs\Windows\v7.0A\include

set GMS_MINOR_VERSION=0
set GMS_MAJOR_VERSION=2
set GMS2-32_SDK=C:\Users\mast\Documents\Scope\DMSDKs\DMSDK%GMS_MAJOR_VERSION%.%GMS_MINOR_VERSION%-32
msbuild /t:Rebuild /p:Configuration=GMS2-32bit /p:Platform=Win32 SerialEMCCD10.vcxproj
if %errorlevel% neq 0 exit /b %errorlevel%

set GMS_MINOR_VERSION=30
set GMS2-32_SDK=C:\Users\mast\Documents\Scope\DMSDKs\DMSDK%GMS_MAJOR_VERSION%.%GMS_MINOR_VERSION%-32
msbuild /t:Rebuild /p:Configuration=GMS2-32bit /p:Platform=Win32 SerialEMCCD10.vcxproj
if %errorlevel% neq 0 exit /b %errorlevel%

set GMS_MINOR_VERSION=31
set GMS2-64_SDK=C:\Users\mast\Documents\Scope\DMSDKs\DMSDK%GMS_MAJOR_VERSION%.%GMS_MINOR_VERSION%-64
msbuild /t:Rebuild /p:Configuration=GMS2-64bit /p:Platform=x64 SerialEMCCD10.vcxproj
if %errorlevel% neq 0 exit /b %errorlevel%

set GMS_MAJOR_VERSION=3
set GMS_MINOR_VERSION=01
set GMS2-64_SDK=C:\Users\mast\Documents\Scope\DMSDKs\DMSDK%GMS_MAJOR_VERSION%.%GMS_MINOR_VERSION%-64
msbuild /t:Rebuild /p:Configuration=GMS2-64bit /p:Platform=x64 SerialEMCCD10.vcxproj
if %errorlevel% neq 0 exit /b %errorlevel%
