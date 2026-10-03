@echo off
rem Zig as C++ compiler for the desktop tools (zig-tool.cmake). ZIG_EXE and ZIG_TARGET come from build-tools.ps1.
"%ZIG_EXE%" c++ -target %ZIG_TARGET% %*
