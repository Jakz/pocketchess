@echo off
rem Zig as a static ARM (hard-float, NEON) compiler for engine builds (zig-arm-engine.cmake). ZIG_EXE is set by build-engine.ps1.
"%ZIG_EXE%" cc -target arm-linux-musleabihf -mcpu=cortex_a7 %*
