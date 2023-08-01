// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using Microsoft.Diagnostics.Monitoring.StartupHook;
using Microsoft.Diagnostics.Tools.Monitor.Profiler;
using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Threading.Tasks;

namespace Microsoft.Diagnostics.Monitoring.HostingStartup.ParameterCapturing.FunctionProbes
{
    internal sealed class FunctionProbesManager : IDisposable
    {
        [DllImport(ProfilerIdentifiers.MutatingProfiler.LibraryRootFileName, CallingConvention = CallingConvention.StdCall, PreserveSig = false)]
        private static extern void RequestFunctionProbeRegistration(ulong enterProbeId);

        [DllImport(ProfilerIdentifiers.MutatingProfiler.LibraryRootFileName, CallingConvention = CallingConvention.StdCall, PreserveSig = false)]
        private static extern void RequestFunctionProbeUninstallation();

        [DllImport(ProfilerIdentifiers.MutatingProfiler.LibraryRootFileName, CallingConvention = CallingConvention.StdCall, PreserveSig = false)]
        private static extern void RequestFunctionProbeInstallation(
            [MarshalAs(UnmanagedType.LPArray)] ulong[] funcIds,
            uint count,
            [MarshalAs(UnmanagedType.LPArray)] uint[] boxingTokens,
            [MarshalAs(UnmanagedType.LPArray)] uint[] boxingTokenCounts);

        private delegate void FunctionProbeInstallationCallback(int hresult);
        private delegate void FunctionProbeUninstallationCallback(int hresult);
        private delegate void FunctionProbeFaultCallback(ulong uniquifier);

        [DllImport(ProfilerIdentifiers.MutatingProfiler.LibraryRootFileName, CallingConvention = CallingConvention.StdCall, PreserveSig = false)]
        private static extern void RegisterFunctionProbeCallbacks(
            FunctionProbeInstallationCallback onInstallation,
            FunctionProbeUninstallationCallback onUninstallation,
            FunctionProbeFaultCallback onFault);

        [DllImport(ProfilerIdentifiers.MutatingProfiler.LibraryRootFileName, CallingConvention = CallingConvention.StdCall, PreserveSig = false)]
        private static extern void UnregisterFunctionProbeCallbacks();

        private const long CapturingStateStopped = default(long);
        private const long CapturingStateActive = 1;

        private long _disposedState;
        private long _capturingState;

        private TaskCompletionSource? _installationTaskSource;
        private TaskCompletionSource? _uninstallationTaskSource;

        public FunctionProbesManager(IFunctionProbes probes)
        {
            ProfilerResolver.InitializeResolver<FunctionProbesManager>();

            RequestFunctionProbeRegistration(FunctionProbesStub.GetProbeFunctionId());
            RegisterFunctionProbeCallbacks(OnInstallation, OnUninstallation, OnFault);

            FunctionProbesStub.Instance = probes;
        }

        private void OnInstallation(int hresult)
        {
            CompleteTaskSource(_installationTaskSource, hresult);
        }

        private void OnUninstallation(int hresult)
        {
            CompleteTaskSource(_uninstallationTaskSource, hresult);
        }

        private void OnFault(ulong uniquifier)
        {

        }

        private static void CompleteTaskSource(TaskCompletionSource? taskCompletionSource, int hresult)
        {
            if (taskCompletionSource == null)
            {
                return;
            }

            Exception? ex = Marshal.GetExceptionForHR(hresult);
            if (ex == null)
            {
                _ = taskCompletionSource.TrySetResult();
            }
            else
            {
                _ = taskCompletionSource.TrySetException(ex);
            }
        }


        public void StopCapturing()
        {
            lock (_requestLocker)
            {
                if (!_isCapturing)
                {
                    return;
                }

                FunctionProbesStub.InstrumentedMethodCache = null;
                RequestFunctionProbeUninstallation();

                _isCapturing = false;
            }
        }

        public void StartCapturing(IList<MethodInfo> methods)
        {
            if (methods.Count == 0)
            {
                throw new ArgumentException(nameof(methods));
            }

            

            Dictionary<ulong, InstrumentedMethod> newMethodCache = new(methods.Count);
            lock (_requestLocker)
            {
                if (_isCapturing)
                {
                    throw new InvalidOperationException();
                }

                List<ulong> functionIds = new(methods.Count);
                List<uint> argumentCounts = new(methods.Count);
                List<uint> boxingTokens = new();

                foreach (MethodInfo method in methods)
                {
                    ulong functionId = method.GetFunctionId();
                    if (functionId == 0)
                    {
                        return;
                    }

                    uint[] methodBoxingTokens = BoxingTokens.GetBoxingTokens(method);
                    if (!newMethodCache.TryAdd(functionId, new InstrumentedMethod(method, methodBoxingTokens)))
                    {
                        // Duplicate, ignore
                        continue;
                    }

                    functionIds.Add(functionId);
                    argumentCounts.Add((uint)methodBoxingTokens.Length);
                    boxingTokens.AddRange(methodBoxingTokens);
                }

                FunctionProbesStub.InstrumentedMethodCache = new ReadOnlyDictionary<ulong, InstrumentedMethod>(newMethodCache);
                RequestFunctionProbeInstallation(
                    functionIds.ToArray(),
                    (uint)functionIds.Count,
                    boxingTokens.ToArray(),
                    argumentCounts.ToArray());

                _isCapturing = true;
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
