Rem Builds all versions of the plugin, run as cmd /C buildAll.bat

set SAVEPATH=%PATH%
GOTO Label
set PATH=c:\Program Files (x86)\Microsoft Visual Studio 10.0\VC\BIN\amd64;C:\windows\Microsoft.NET\Framework64\v4.0.30319;C:\windows\Microsoft.NET\Framework64\v3.5;c:\Program Files (x86)\Microsoft Visual Studio 10.0\VC\VCPackages;c:\Program Files (x86)\Microsoft Visual Studio 10.0\Common7\IDE;c:\Program Files (x86)\Microsoft Visual Studio 10.0\Common7\Tools;C:\Program Files (x86)\HTML Help Workshop;C:\Program Files (x86)\Microsoft SDKs\Windows\v7.0A\bin\NETFX 4.0 Tools\x64;C:\Program Files (x86)\Microsoft SDKs\Windows\v7.0A\bin\x64;C:\Program Files (x86)\Microsoft SDKs\Windows\v7.0A\bin;%PATH%
set LIB=c:\Program Files (x86)\Microsoft Visual Studio 10.0\VC\LIB\amd64;c:\Program Files (x86)\Microsoft Visual Studio 10.0\VC\ATLMFC\LIB\amd64;C:\Program Files (x86)\Microsoft SDKs\Windows\v7.0A\lib\x64
set INCLUDE=c:\Program Files (x86)\Microsoft Visual Studio 10.0\VC\INCLUDE;c:\Program Files (x86)\Microsoft Visual Studio 10.0\VC\ATLMFC\INCLUDE;C:\Program Files (x86)\Microsoft SDKs\Windows\v7.0A\include

set DM_PLUGIN_SUFFIX=
set GMS_MINOR_VERSION=0
set GMS_MAJOR_VERSION=2
set GMS2-32_SDK=C:\Users\mast\Documents\Scope\DMSDKs\DMSDK%GMS_MAJOR_VERSION%.%GMS_MINOR_VERSION%-32
msbuild /t:Rebuild /p:Configuration=GMS2-32bit /p:Platform=Win32 SerialEMCCD14.vcxproj
if %errorlevel% neq 0 exit /b %errorlevel%

set GMS_MINOR_VERSION=30
set GMS2-32_SDK=C:\Users\mast\Documents\Scope\DMSDKs\DMSDK%GMS_MAJOR_VERSION%.%GMS_MINOR_VERSION%-32
msbuild /t:Rebuild /p:Configuration=GMS2-32bit /p:Platform=Win32 SerialEMCCD14.vcxproj
if %errorlevel% neq 0 exit /b %errorlevel%

set GMS_MINOR_VERSION=31
set GMS2-64_SDK=C:\Users\mast\Documents\Scope\DMSDKs\DMSDK%GMS_MAJOR_VERSION%.%GMS_MINOR_VERSION%-64
msbuild /t:Rebuild /p:Configuration=GMS2-64bit /p:Platform=x64 SerialEMCCD14.vcxproj
if %errorlevel% neq 0 exit /b %errorlevel%

set GMS_MAJOR_VERSION=3
set GMS_MINOR_VERSION=01
set GMS2-64_SDK=C:\Users\mast\Documents\Scope\DMSDKs\DMSDK%GMS_MAJOR_VERSION%.%GMS_MINOR_VERSION%-64
msbuild /t:Rebuild /p:Configuration=GMS2-64bit /p:Platform=x64 SerialEMCCD14.vcxproj
if %errorlevel% neq 0 exit /b %errorlevel%

Rem VS2015 build - reset the path and lib/include
set PATH=C:\Program Files (x86)\Microsoft Visual Studio 14.0\Common7\IDE\CommonExtensions\Microsoft\TestWindow;C:\Program Files (x86)\MSBuild\14.0\bin\amd64;C:\Program Files (x86)\Microsoft Visual Studio 14.0\VC\BIN\amd64;C:\WINDOWS\Microsoft.NET\Framework64\v4.0.30319;C:\Program Files (x86)\Microsoft Visual Studio 14.0\VC\VCPackages;C:\Program Files (x86)\Microsoft Visual Studio 14.0\Common7\IDE;C:\Program Files (x86)\Microsoft Visual Studio 14.0\Common7\Tools;C:\Program Files (x86)\HTML Help Workshop;C:\Program Files (x86)\Microsoft Visual Studio 14.0\Team Tools\Performance Tools\x64;C:\Program Files (x86)\Microsoft Visual Studio 14.0\Team Tools\Performance Tools;C:\Program Files (x86)\Windows Kits\8.1\bin\x64;C:\Program Files (x86)\Windows Kits\8.1\bin\x86;C:\Program Files (x86)\Microsoft SDKs\Windows\v10.0A\bin\NETFX 4.6.1 Tools\x64\;%SAVEPATH%

set LIB=C:\Program Files (x86)\Microsoft Visual Studio 14.0\VC\LIB\amd64;C:\Program Files (x86)\Microsoft Visual Studio 14.0\VC\ATLMFC\LIB\amd64;C:\Program Files (x86)\Windows Kits\10\lib\10.0.10240.0\ucrt\x64;C:\Program Files (x86)\Windows Kits\NETFXSDK\4.6.1\lib\um\x64;C:\Program Files (x86)\Windows Kits\8.1\lib\winv6.3\um\x64
set INCLUDE=C:\Program Files (x86)\Microsoft Visual Studio 14.0\VC\INCLUDE;C:\Program Files (x86)\Microsoft Visual Studio 14.0\VC\ATLMFC\INCLUDE;C:\Program Files (x86)\Windows Kits\10\include\10.0.10240.0\ucrt;C:\Program Files (x86)\Windows Kits\NETFXSDK\4.6.1\include\um;C:\Program Files (x86)\Windows Kits\8.1\include\\shared;C:\Program Files (x86)\Windows Kits\8.1\include\\um;C:\Program Files (x86)\Windows Kits\8.1\include\\winrt

set GMS_MAJOR_VERSION=3
set GMS_MINOR_VERSION=31
set GMS2-64_SDK=C:\Users\mast\Documents\Scope\DMSDKs\DMSDK%GMS_MAJOR_VERSION%.%GMS_MINOR_VERSION%-64
msbuild /t:Rebuild /p:Configuration=GMS3.31-64 /p:Platform=x64 SerialEMCCD14.vcxproj
if %errorlevel% neq 0 exit /b %errorlevel%

set GMS_MINOR_VERSION=42
set GMS2-64_SDK=C:\Users\mast\Documents\Scope\DMSDKs\DMSDK%GMS_MAJOR_VERSION%.%GMS_MINOR_VERSION%-64
msbuild /t:Rebuild /p:Configuration=GMS3.31-64 /p:Platform=x64 SerialEMCCD14.vcxproj
if %errorlevel% neq 0 exit /b %errorlevel%

set GMS_MINOR_VERSION=50
set GMS2-64_SDK=C:\Users\mast\Documents\Scope\DMSDKs\DMSDK%GMS_MAJOR_VERSION%.%GMS_MINOR_VERSION%-64
msbuild /t:Rebuild /p:Configuration=GMS3.5-IOMP /p:Platform=x64 SerialEMCCD14.vcxproj

set GMS_MINOR_VERSION=60
set GMS2-64_SDK=C:\Users\mast\Documents\Scope\DMSDKs\DMSDK%GMS_MAJOR_VERSION%.%GMS_MINOR_VERSION%-64
set DM_PLUGIN_SUFFIX=_NoBCG
msbuild /t:Rebuild /p:Configuration=GMS3.5-IOMP /p:Platform=x64 SerialEMCCD14.vcxproj
if %errorlevel% neq 0 exit /b %errorlevel%

:Label
set MSVS2019=C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional
set WINKIT10=C:\Program Files (x86)\Windows Kits\10
set PATH=%MSVS2019%\VC\Tools\MSVC\14.29.30133\bin\HostX64\x64;%MSVS2019%\Common7\IDE\VC\VCPackages;%MSVS2019%\Common7\IDE\CommonExtensions\Microsoft\TestWindow;%MSVS2019%\Common7\IDE\CommonExtensions\Microsoft\TeamFoundation\Team Explorer;%MSVS2019%\MSBuild\Current\bin\Roslyn;%MSVS2019%\Team Tools\Performance Tools\x64;%MSVS2019%\Team Tools\Performance Tools;C:\Program Files (x86)\Microsoft Visual Studio\Shared\Common\VSPerfCollectionTools\vs2019\\x64;C:\Program Files (x86)\Microsoft Visual Studio\Shared\Common\VSPerfCollectionTools\vs2019\;C:\Program Files (x86)\Microsoft SDKs\Windows\v10.0A\bin\NETFX 4.8 Tools\x64\;C:\Program Files (x86)\HTML Help Workshop;%MSVS2019%\Common7\Tools\devinit;%WINKIT10%\bin\10.0.19041.0\x64;%WINKIT10%\bin\x64;%MSVS2019%\\MSBuild\Current\Bin;C:\windows\Microsoft.NET\Framework64\v4.0.30319;%MSVS2019%\Common7\IDE\;%MSVS2019%\Common7\Tools\;%MSVS2019%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%MSVS2019%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja
set LIB=%MSVS2019%\VC\Tools\MSVC\14.29.30133\ATLMFC\lib\x64;%MSVS2019%\VC\Tools\MSVC\14.29.30133\lib\x64;C:\Program Files (x86)\Windows Kits\NETFXSDK\4.8\lib\um\x64;%WINKIT10%\lib\10.0.19041.0\ucrt\x64;%WINKIT10%\lib\10.0.19041.0\um\x64
set INCLUDE=%MSVS2019%\VC\Tools\MSVC\14.29.30133\ATLMFC\include;%MSVS2019%\VC\Tools\MSVC\14.29.30133\include;C:\Program Files (x86)\Windows Kits\NETFXSDK\4.8\include\um;%WINKIT10%\include\10.0.19041.0\ucrt;%WINKIT10%\include\10.0.19041.0\shared;%WINKIT10%\include\10.0.19041.0\um;%WINKIT10%\include\10.0.19041.0\winrt;%WINKIT10%\include\10.0.19041.0\cppwinrt

set GMS_MINOR_VERSION=62
set GMS2-64_SDK=C:\Users\mast\Documents\Scope\DMSDKs\DMSDK%GMS_MAJOR_VERSION%.%GMS_MINOR_VERSION%-64
set DM_PLUGIN_SUFFIX=_NoBCG
msbuild /t:Rebuild /p:Configuration=GMS3.5-IOMP /p:Platform=x64 SerialEMCCD16.vcxproj
if %errorlevel% neq 0 exit /b %errorlevel%
