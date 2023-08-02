// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using Microsoft.Diagnostics.Monitoring.StartupHook;
using Microsoft.Diagnostics.Tools.Monitor.Profiler;
using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Threading;
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

        private delegate void FunctionProbeRegistrationCallback(int hresult);
        private delegate void FunctionProbeInstallationCallback(int hresult);
        private delegate void FunctionProbeUninstallationCallback(int hresult);
        private delegate void FunctionProbeFaultCallback(ulong uniquifier);

        [DllImport(ProfilerIdentifiers.MutatingProfiler.LibraryRootFileName, CallingConvention = CallingConvention.StdCall, PreserveSig = false)]
        private static extern void RegisterFunctionProbeCallbacks(
            FunctionProbeRegistrationCallback onRegistration,
            FunctionProbeInstallationCallback onInstallation,
            FunctionProbeUninstallationCallback onUninstallation,
            FunctionProbeFaultCallback onFault);

        [DllImport(ProfilerIdentifiers.MutatingProfiler.LibraryRootFileName, CallingConvention = CallingConvention.StdCall, PreserveSig = false)]
        private static extern void UnregisterFunctionProbeCallbacks();

        private const long CapturingStateStopped = default(long);
        private const long CapturingStateStopping = 1;
        private const long CapturingStateStarted = 2;
        private const long CapturingStateStarting = 3;

        private long _disposedState;
        private long _capturingState;

        private readonly TaskCompletionSource _probeRegistrationSource = new(TaskCreationOptions.RunContinuationsAsynchronously);
        
        private TaskCompletionSource? _installationTaskSource;
        private TaskCompletionSource? _uninstallationTaskSource;

        public event EventHandler<ulong>? OnProbeFault;

        public Task InitializationTask { get { return _probeRegistrationSource.Task; } }

        public FunctionProbesManager(IFunctionProbes probes)
        {
            ProfilerResolver.InitializeResolver<FunctionProbesManager>();

            RegisterFunctionProbeCallbacks(OnRegistration, OnInstallation, OnUninstallation, OnFault);
            RequestFunctionProbeRegistration(FunctionProbesStub.GetProbeFunctionId());

            FunctionProbesStub.Instance = probes;
        }

        private void OnRegistration(int hresult)
        {
            CompleteTaskSource(_probeRegistrationSource, hresult);
        }

        private void OnInstallation(int hresult)
        {
            _capturingState = CapturingStateStarted;
            CompleteTaskSource(_installationTaskSource, hresult);
        }

        private void OnUninstallation(int hresult)
        {
            _capturingState = CapturingStateStopped;
            CompleteTaskSource(_uninstallationTaskSource, hresult);
        }

        private void OnFault(ulong uniquifier)
        {
            OnProbeFault?.Invoke(this, uniquifier);
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

        private void StopCapturingCore()
        {
            if (CapturingStateStarted == Interlocked.CompareExchange(ref _capturingState, CapturingStateStopping, CapturingStateStarted))
            {
                FunctionProbesStub.InstrumentedMethodCache = null;
                RequestFunctionProbeUninstallation();
            }
        }

        public Task StopCapturingAsync()
        {
            StopCapturingCore();
            return _uninstallationTaskSource?.Task ?? Task.CompletedTask;
        }

        public Task StartCapturingAsync(IList<MethodInfo> methods)
        {
            if (methods.Count == 0)
            {
                throw new ArgumentException(nameof(methods));
            }

            if (CapturingStateStopped != Interlocked.CompareExchange(ref _capturingState, CapturingStateStarting, CapturingStateStopped))
            {
                throw new InvalidOperationException();
            }

            try
            {
                Dictionary<ulong, InstrumentedMethod> newMethodCache = new(methods.Count);
                List<ulong> functionIds = new(methods.Count);
                List<uint> argumentCounts = new(methods.Count);
                List<uint> boxingTokens = new();

                foreach (MethodInfo method in methods)
                {
                    ulong functionId = method.GetFunctionId();
                    if (functionId == 0)
                    {
                        throw new NotSupportedException(method.Name);
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

                _capturingState = CapturingStateStarted;
            }
            catch
            {
                _capturingState = CapturingStateStopped;
                throw;
            }

            _installationTaskSource = new(TaskCreationOptions.RunContinuationsAsynchronously);
            _uninstallationTaskSource = new(TaskCreationOptions.RunContinuationsAsynchronously);

            return _installationTaskSource.Task;
        }

        public void Dispose()
        {
            if (!DisposableHelper.CanDispose(ref _disposedState))
                return;

            FunctionProbesStub.Instance = null;

            _installationTaskSource?.TrySetCanceled();
            _uninstallationTaskSource?.TrySetCanceled();

            try
            {
                UnregisterFunctionProbeCallbacks();
            }
            catch
            {
            }
        }
    }
}
