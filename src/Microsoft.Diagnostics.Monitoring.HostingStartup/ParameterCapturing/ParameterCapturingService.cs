// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using Microsoft.Diagnostics.Monitoring.HostingStartup.ParameterCapturing.FunctionProbes;
using Microsoft.Diagnostics.Tools.Monitor.Profiler;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using System;
using System.Collections;
using System.Collections.Generic;
using System.Diagnostics;
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


        private readonly IFunctionProbes? _probes;
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
                _instrumentedMethodCache = new();
                _probes = new LogEmittingProbes(_logger, _instrumentedMethodCache);
                RegisterFunctionProbe(_probes.GetProbeFunctionId());
                _isAvailable = true;
            }
            catch (Exception ex)
            {
                _logger.Log(LogLevel.Debug, ex.ToString());
            }
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
                bool[] supportedArgs = new bool[methodBoxingTokens.Length];
                for (int i = 0; i < methodBoxingTokens.Length; i++)
                {
                    supportedArgs[i] = methodBoxingTokens[i] != (int)TypeCode.Empty;
                }

                _instrumentedMethodCache!.Add(method, supportedArgs);
                functionIds.Add(functionId);

                argumentCounts.Add((uint)methodBoxingTokens.Length);
                boxingTokens.AddRange(methodBoxingTokens);
            }

            RequestFunctionProbeInstallation(functionIds.ToArray(), (uint)functionIds.Count, boxingTokens.ToArray(), argumentCounts.ToArray());
        }

        protected override Task ExecuteAsync(CancellationToken stoppingToken)
        {
            if (!_isAvailable || _probes == null || _instrumentedMethodCache == null)
            {
                return Task.Delay(Timeout.Infinite, stoppingToken);
            }



            _instrumentedMethodCache.Clear();
            return Task.CompletedTask;
        }
    }
}
