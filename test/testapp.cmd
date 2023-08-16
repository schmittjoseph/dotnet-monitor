@echo off
set DOTNET_DiagnosticPorts=diag.sock,suspend
set DotnetMonitor_Profiler_StdErrLogger_Level=Info
set DOTNET_STARTUP_HOOKS=C:\Users\joschmit\work\dotnet-monitor\artifacts\packages\Release\Shipping\test\tools\net6.0\any\shared\any\net6.0\Microsoft.Diagnostics.Monitoring.StartupHook.dll
REM windbgx -g -o 
REM C:\Users\joschmit\work\buggy-demo-code\src\BuggyDemoWeb\bin\Debug\net7.0\BuggyDemoWeb.exe
REM C:\Users\joschmit\work\dotnet-monitor\dotnet.cmd C:\Users\joschmit\work\buggy-demo-code\src\BuggyDemoWeb\bin\Debug\net8.0\BuggyDemoWeb.dll

@REM pushd C:\Users\joschmit\work\buggy-demo-code\src\BuggyDemoWeb\bin\Debug\net7.0
@REM C:\Users\joschmit\work\buggy-demo-code\src\BuggyDemoWeb\bin\Debug\net7.0\BuggyDemoWeb.exe
@REM popd

pushd C:\Users\joschmit\work\Benchmarks\src\BenchmarksApps\Mvc\bin\Release\net7.0
C:\Users\joschmit\work\Benchmarks\src\BenchmarksApps\Mvc\bin\Release\net7.0\Mvc.exe
popd