// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using Microsoft.Diagnostics.Monitoring.HostingStartup.ParameterCapturing.Eventing;
using Microsoft.Diagnostics.Monitoring.HostingStartup.ParameterCapturing.FunctionProbes;
using Microsoft.Diagnostics.Monitoring.StartupHook;
using Microsoft.Diagnostics.Monitoring.WebApi.Models;
using Microsoft.Diagnostics.Tools.Monitor.ParameterCapturing;
using Microsoft.Diagnostics.Tools.Monitor.StartupHook;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using System.Threading;
using System.Threading.Channels;
using System.Threading.Tasks;

namespace Microsoft.Diagnostics.Monitoring.HostingStartup.ParameterCapturing
{
    internal sealed class ParameterCapturingService : BackgroundService, IDisposable
    {
        private long _disposedState;

        private ParameterCapturingEvents.ServiceNotAvailableReason _notAvailableReason = ParameterCapturingEvents.ServiceNotAvailableReason.None;
        private string _notAvailableDetails = string.Empty;

        private readonly FunctionProbesManager? _probeManager;
        private readonly ParameterCapturingEventSource _eventSource = new();
        private readonly ILogger? _logger;

        private Channel<StartCapturingParametersPayload>? _requests;
        private Channel<bool>? _stopRequests;

        public ParameterCapturingService(IServiceProvider services)
        {
            // Register the command callbacks (if possible) first so that dotnet-monitor
            // can be notified of any initialization errors when it tries to invoke the commands.
            SharedInternals.MessageDispatcher?.RegisterCallback<StartCapturingParametersPayload>(
                IpcCommand.StartCapturingParameters,
                OnStartMessage);

            SharedInternals.MessageDispatcher?.RegisterCallback<EmptyPayload>(
                IpcCommand.StopCapturingParameters,
                OnStopMessage);

            try
            {
                _logger = services.GetService<ILogger>();
                if (_logger == null)
                {
                    throw new NotSupportedException(ParameterCapturingStrings.FeatureUnsupported_NoLogger);
                }

                ArgumentNullException.ThrowIfNull(SharedInternals.MessageDispatcher);

                _requests = Channel.CreateBounded<StartCapturingParametersPayload>(new BoundedChannelOptions(capacity: 1)
                {
                    FullMode = BoundedChannelFullMode.DropWrite
                });
                _stopRequests = Channel.CreateBounded<bool>(new BoundedChannelOptions(capacity: 2)
                {
                    FullMode = BoundedChannelFullMode.DropWrite,
                    SingleReader = true,
                    SingleWriter = true
                });

                _probeManager = new FunctionProbesManager(new LogEmittingProbes(_logger));
            }
            catch (Exception ex)
            {
                UnrecoverableInternalFault(ex);
                _requests?.Writer.TryComplete();
                _stopRequests?.Writer.TryComplete();
            }
        }

        private void OnStartMessage(StartCapturingParametersPayload payload)
        {
            if (payload.Methods.Length == 0)
            {
                _eventSource.FailedToCapture(new ArgumentException(nameof(payload.Methods)));
                return;
            }

            if (_requests?.Writer.TryWrite(payload) != true)
            {
                if (!IsAvailable())
                {
                    _eventSource.ServiceNotAvailable(_notAvailableReason, _notAvailableDetails);
                    return;
                }
                else
                {
                    // The channel is full, which should never happen if dotnet-monitor is properly rate limiting requests.
                    _eventSource.FailedToCapture(ParameterCapturingEvents.CapturingFailedReason.TooManyRequests, string.Empty);
                }
            }
        }

        private bool IsAvailable()
        {
            return _notAvailableReason == ParameterCapturingEvents.ServiceNotAvailableReason.None;
        }

        private void OnStopMessage(EmptyPayload _)
        {
            if (_stopRequests?.Writer.TryWrite(true) != true)
            {
                if (!IsAvailable())
                {
                    _eventSource.ServiceNotAvailable(_notAvailableReason, _notAvailableDetails);
                    return;
                }
                else
                {
                    // The channel is full which is OK as stop requests aren't tied to a specific operation.
                }
            }
        }

        private void StartCapturing(StartCapturingParametersPayload request)
        {
            if (!IsAvailable())
            {
                throw new InvalidOperationException();
            }

            Dictionary<(string, string), List<MethodInfo>> methodCache = GenerateMethodCache(request.Methods);

            List<MethodInfo> methods = new(request.Methods.Length);
            List<MethodDescription> unableToResolve = new();
            for (int i = 0; i < request.Methods.Length; i++)
            {
                MethodDescription methodDescription = request.Methods[i];

                List<MethodInfo> resolvedMethods = ResolveMethod(methodCache, methodDescription);
                if (resolvedMethods.Count == 0)
                {
                    unableToResolve.Add(methodDescription);
                }

                methods.AddRange(resolvedMethods);
            }

            if (unableToResolve.Count > 0)
            {
                UnresolvedMethodsExceptions ex = new(unableToResolve);
                _logger!.Log(LogLevel.Warning, ex, null, Array.Empty<object>());
                throw ex;
            }

            _probeManager!.StartCapturing(methods);
            _logger!.LogInformation(
                ParameterCapturingStrings.StartParameterCapturingFormatString,
                request.Duration,
                request.Methods.Length);
        }

        private static Dictionary<(string, string), List<MethodInfo>> GenerateMethodCache(MethodDescription[] methodDescriptions)
        {
            Dictionary<(string, string), List<MethodInfo>> cache = new();

            Assembly[] assemblies = AppDomain.CurrentDomain.GetAssemblies().Where(assembly => !assembly.ReflectionOnly && !assembly.IsDynamic).ToArray();
            Dictionary<string, List<Module>> dllNameToModules = new(StringComparer.InvariantCultureIgnoreCase);
            foreach (Assembly assembly in assemblies)
            {
                foreach (Module module in assembly.GetModules())
                {
                    if (!dllNameToModules.TryGetValue(module.Name, out List<Module>? moduleList))
                    {
                        moduleList = new List<Module>();
                        dllNameToModules[module.Name] = moduleList;
                    }

                    moduleList.Add(module);
                }
            }

            foreach (MethodDescription methodDescription in methodDescriptions)
            {
                if (cache.ContainsKey((methodDescription.ModuleName, methodDescription.ClassName)))
                {
                    continue;
                }

                if (!dllNameToModules.TryGetValue(methodDescription.ModuleName, out List<Module>? possibleModules))
                {
                    throw new InvalidOperationException();
                }

                List<MethodInfo> classMethods = new();

                foreach (Module module in possibleModules)
                {
                    try
                    {
                        MethodInfo[]? allMethods = module.Assembly.GetType(methodDescription.ClassName)?.GetMethods(BindingFlags.Public |
                            BindingFlags.NonPublic |
                            BindingFlags.Instance |
                            BindingFlags.Static |
                            BindingFlags.FlattenHierarchy);

                        if (allMethods == null)
                        {
                            continue;
                        }

                        classMethods.AddRange(allMethods);
                    }
                    catch
                    {
                    }
                }

                cache.Add((methodDescription.ModuleName, methodDescription.ClassName), classMethods);
            }

            return cache;
        }

        private void StopCapturing()
        {
            if (!IsAvailable())
            {
                return;
            }

            _logger!.LogInformation(ParameterCapturingStrings.StopParameterCapturing);
            _probeManager!.StopCapturing();
        }

        private static List<MethodInfo> ResolveMethod(Dictionary<(string, string), List<MethodInfo>> methodCache, MethodDescription methodDescription)
        {
            List<MethodInfo> methods = new();

            if (!methodCache.TryGetValue((methodDescription.ModuleName, methodDescription.ClassName), out List<MethodInfo>? possibleMethods))
            {
                return methods;
            }


            foreach (MethodInfo method in possibleMethods)
            {
                if (method.Name == methodDescription.MethodName)
                {
                    methods.Add(method);
                }
            }

            return methods;
        }

        protected override async Task ExecuteAsync(CancellationToken stoppingToken)
        {
            if (!IsAvailable())
            {
                return;
            }

            stoppingToken.Register(StopCapturing);
            while (!stoppingToken.IsCancellationRequested)
            {
                StartCapturingParametersPayload req = await _requests!.Reader.ReadAsync(stoppingToken);
                try
                {
                    StartCapturing(req);
                    _eventSource.CapturingStart();
                }
                catch (Exception ex)
                {
                    _eventSource.FailedToCapture(ex);
                    continue;
                }

                using CancellationTokenSource cts = CancellationTokenSource.CreateLinkedTokenSource(stoppingToken);

                Task stopSignalTask = _stopRequests!.Reader.WaitToReadAsync(cts.Token).AsTask();
                _ = await Task.WhenAny(stopSignalTask, Task.Delay(req.Duration, cts.Token)).ConfigureAwait(false);

                // Signal the other stop condition tasks to cancel
                cts.Cancel();

                // Clear any stop requests
                _ = _stopRequests.Reader.TryRead(out _);

                try
                {
                    StopCapturing();
                    _eventSource.CapturingStop();
                }
                catch (Exception ex)
                {
                    // We're in a faulted state from an internal exception so there's
                    // nothing else that can be safely done for the remainder of the app's lifetime.
                    UnrecoverableInternalFault(ex);
                    return;
                }
            }
        }

        private void UnrecoverableInternalFault(Exception ex)
        {
            if (ex is NotSupportedException)
            {
                _notAvailableReason = ParameterCapturingEvents.ServiceNotAvailableReason.NotSupported;
                _notAvailableDetails = ex.Message;
            }
            else
            {
                _notAvailableReason = ParameterCapturingEvents.ServiceNotAvailableReason.InternalError;
                _notAvailableDetails = ex.ToString();
            }

            _eventSource.ServiceNotAvailable(_notAvailableReason, _notAvailableDetails);

            _ = _requests?.Writer.TryComplete();
            _ = _stopRequests?.Writer.TryComplete();
        }

        public override void Dispose()
        {
            if (!DisposableHelper.CanDispose(ref _disposedState))
                return;

            SharedInternals.MessageDispatcher?.UnregisterCallback(IpcCommand.StartCapturingParameters);
            SharedInternals.MessageDispatcher?.UnregisterCallback(IpcCommand.StopCapturingParameters);

            try
            {
                _probeManager?.Dispose();
            }
            catch
            {

            }

            base.Dispose();
        }
    }
}
