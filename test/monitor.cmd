@echo off
REM windbgx -g -o 

set DotnetMonitor_Experimental_Feature_ParameterCapturing=1
C:\Users\joschmit\work\dotnet-monitor\dotnet.cmd C:\Users\joschmit\work\dotnet-monitor\artifacts\packages\Release\Shipping\test\tools\net6.0\any\dotnet-monitor.dll collect --no-auth --configuration-file-path C:\Users\joschmit\work\dotnet-monitor\.vscode\innerloop\config\settings.json 