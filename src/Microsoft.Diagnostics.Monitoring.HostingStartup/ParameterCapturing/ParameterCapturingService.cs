// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using Microsoft.Diagnostics.Monitoring.HostingStartup.ParameterCapturing.FunctionProbes;
using Microsoft.Diagnostics.Tools.Monitor.Profiler;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;

namespace Microsoft.Diagnostics.Monitoring.HostingStartup.ParameterCapturing
{
    internal sealed class ParameterCapturingService : BackgroundService
    {
        [DllImport(ProfilerIdentifiers.LibraryRootFileName, CallingConvention = CallingConvention.StdCall, PreserveSig = false)]
        private static extern void RegisterFunctionProbe(ulong enterProbeId);

        [DllImport(ProfilerIdentifiers.LibraryRootFileName, CallingConvention = CallingConvention.StdCall, PreserveSig = false)]
        private static extern void RequestFunctionUninstallation();

        [DllImport(ProfilerIdentifiers.LibraryRootFileName, CallingConvention = CallingConvention.StdCall, PreserveSig = false)]
        private static extern void RequestFunctionProbeInstallation(
            [MarshalAs(UnmanagedType.LPArray)] ulong[] funcIds,
            uint count,
            [MarshalAs(UnmanagedType.LPArray)] uint[] boxingTokens,
            [MarshalAs(UnmanagedType.LPArray)] uint[] boxingTokenCounts);


        private readonly InstrumentedMethodCache? _instrumentedMethodCache;
        private readonly ILogger? _logger;
        private readonly bool _isAvailable;

        public ParameterCapturingService(IServiceProvider services)
        {
            _logger = services.GetService<ILogger<ParameterCapturingService>>();
            if (_logger == null)
            {
                return;
            }

            try
            {
                // NativeLibrary.SetDllImportResolver(typeof(ParameterCapturingService).Assembly, ResolveDllImport);

                _instrumentedMethodCache = new();
                LogEmittingProbes.Init(_logger, _instrumentedMethodCache);
                RegisterFunctionProbe(LogEmittingProbes.GetProbeFunctionId());
                _isAvailable = true;
            }
            catch (Exception ex)
            {
                _logger.Log(LogLevel.Debug, ex.ToString());
            }
        }

        private static IntPtr ResolveDllImport(string libraryName, Assembly assembly, DllImportSearchPath? searchPath)
        {
            // DllImport for Windows automatically loads in-memory modules (such as the profiler). This is not the case for Linux/MacOS.
            // If we fail resolving the DllImport, we have to load the profiler ourselves.

            string? loadedProfilerPath = Environment.GetEnvironmentVariable(ProfilerIdentifiers.EnvironmentVariables.ReflectivePath);
            // This environment variable should only ever be set by our profiler if it has been loaded, so we don't risk accidentally
            // loading the profiler if it isn't already present.
            if (!string.IsNullOrEmpty(loadedProfilerPath) && NativeLibrary.TryLoad(loadedProfilerPath, out IntPtr handle))
            {
                return handle;
            }

            return IntPtr.Zero;
        }

        private void RequestProbeInstallation(IEnumerable<MethodInfo> methods)
        {
            List<ulong> functionIds = new(methods.Count());
            List<uint> argumentCounts = new(methods.Count());
            List<uint> boxingTokens = new();

            foreach (MethodInfo method in methods)
            {
                ulong functionId = (ulong)method.MethodHandle.Value.ToInt64();

                uint[] methodBoxingTokens = BoxingTokens.GetBoxingTokens(method);
                bool[] supportedArgs = BoxingTokens.GetSupportedArgs(methodBoxingTokens);

                _instrumentedMethodCache!.Add(method, supportedArgs);
                functionIds.Add(functionId);

                argumentCounts.Add((uint)methodBoxingTokens.Length);
                boxingTokens.AddRange(methodBoxingTokens);
            }

            RequestFunctionProbeInstallation(functionIds.ToArray(), (uint)functionIds.Count, boxingTokens.ToArray(), argumentCounts.ToArray());
        }

        protected override Task ExecuteAsync(CancellationToken stoppingToken)
        {
            if (!_isAvailable || _instrumentedMethodCache == null)
            {
                return Task.Delay(Timeout.Infinite, stoppingToken);
            }



            _instrumentedMethodCache.Clear();
            return Task.CompletedTask;
        }
    }
}
