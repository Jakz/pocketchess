@echo off
rem Zig as a Windows x64 compiler for engine builds (zig-windows-x64.cmake). ZIG_EXE is set by build-engine.ps1.
"%ZIG_EXE%" c++ -target x86_64-windows-gnu %*
