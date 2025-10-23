@echo off
set QT_ROOT_DIR=C:\Qt\6.9.3\msvc2022_64
set CMAKE_C_COMPILER=cl
set CMAKE_CXX_COMPILER=cl
call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=amd64 -host_arch=amd64
cmake --preset release-vcpkg
