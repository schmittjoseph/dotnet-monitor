// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using Microsoft.Diagnostics.Monitoring.StartupHook;
using Microsoft.Diagnostics.Tools.Monitor.Profiler;
using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Diagnostics;
using System.Linq;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Security.Cryptography.Xml;
using System.Text;
using System.Threading;

namespace Microsoft.Diagnostics.Monitoring.HostingStartup.ParameterCapturing.FunctionProbes
{
    internal sealed class FunctionProbesManager : IDisposable
    {
        [DllImport(ProfilerIdentifiers.LibraryRootFileName, CallingConvention = CallingConvention.StdCall, PreserveSig = false)]
        private static extern void RequestFunctionProbeRegistration(ulong enterProbeId);

        [DllImport(ProfilerIdentifiers.LibraryRootFileName, CallingConvention = CallingConvention.StdCall, PreserveSig = false)]
        private static extern void RequestFunctionProbeUninstallation();

        [DllImport(ProfilerIdentifiers.LibraryRootFileName, CallingConvention = CallingConvention.StdCall, PreserveSig = false)]
        private static extern void RequestFunctionProbeInstallation(
            [MarshalAs(UnmanagedType.LPArray)] ulong[] funcIds,
            uint count,
            [MarshalAs(UnmanagedType.LPArray)] uint[] boxingTokens,
            [MarshalAs(UnmanagedType.LPArray)] uint[] boxingTokenCounts);
       
        private readonly object _requestLocker = new();
        private long _disposedState;

        public FunctionProbesManager(IFunctionProbes probes)
        {
            ProfilerResolver.InitializeResolver<FunctionProbesManager>();

            RequestFunctionProbeRegistration(FunctionProbesStub.GetProbeFunctionId());
            FunctionProbesStub.Instance = probes;
        }

        public void StopCapturing()
        {
            lock (_requestLocker)
            {
                FunctionProbesStub.InstrumentedMethodCache = null;
                RequestFunctionProbeUninstallation();
            }
        }

        public void StartCapturing(IList<MethodInfo> methods)
        {
            if (methods.Count == 0)
            {
                throw new ArgumentException(nameof(methods));
            }

            ConcurrentDictionary<ulong, InstrumentedMethod> newMethodCache = new();
            lock (_requestLocker)
            {
                FunctionProbesStub.InstrumentedMethodCache = null;
                List<ulong> functionIds = new(methods.Count);
                List<uint> argumentCounts = new(methods.Count);
                List<uint> boxingTokens = new();
                Console.WriteLine("START: REQ PROCESSING");

                HashSet<Assembly> assemblies = AppDomain.CurrentDomain.GetAssemblies().Where(assembly => !assembly.ReflectionOnly && !assembly.IsDynamic).ToHashSet();
                foreach (MethodInfo method in methods)
                {
                    string fullName = $"{method.Module.Name}.{method.DeclaringType?.FullName}.{method.Name}";
                    if (fullName.Contains("Microsoft.Diagnostics.Monitoring") ||
                        false
                        )
                    {
                      //  Console.WriteLine($"SKip: {fullName}");
                        continue;
                    }

                    if (!assemblies.Contains(method.Module.Assembly) ||
                        (method.DeclaringType != null && !assemblies.Contains(method.DeclaringType!.Assembly)))
                    {
                        Console.WriteLine($"Skipping lazily loaded method: {fullName}");
                        continue;
                    }


                    ulong functionId = method.GetFunctionId();
                    if (functionId == 0)
                    {
                        return;
                    }


                    uint[] methodBoxingTokens = BoxingTokens.GetBoxingTokens(method);
                    if (!newMethodCache.TryAdd(functionId, new InstrumentedMethod(method, methodBoxingTokens)))
                    {
                        continue;
                    }

                    functionIds.Add(functionId);
                    argumentCounts.Add((uint)methodBoxingTokens.Length);
                    boxingTokens.AddRange(methodBoxingTokens);
                }
                Console.WriteLine($"STOP: REQ PROCESSING: Installing hooks into {functionIds.Count} methods");

                FunctionProbesStub.InstrumentedMethodCache = new ReadOnlyDictionary<ulong, InstrumentedMethod>(newMethodCache);
                RequestFunctionProbeInstallation(
                    functionIds.ToArray(),
                    (uint)functionIds.Count,
                    boxingTokens.ToArray(),
                    argumentCounts.ToArray());
                Console.WriteLine("SENT TO PROFILER");
            }
        }

        public void Dispose()
        {
            if (!DisposableHelper.CanDispose(ref _disposedState))
                return;

            FunctionProbesStub.Instance = null;
            StopCapturing();
        }
    }
}
