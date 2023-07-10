﻿// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using Microsoft.Diagnostics.Monitoring.HostingStartup.ParameterCapturing.Eventing;
using Microsoft.Diagnostics.Monitoring.HostingStartup.ParameterCapturing.FunctionProbes;
using Microsoft.Diagnostics.Monitoring.StartupHook;
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
        private readonly bool _isAvailable;

        private long _activeRequests;

        private readonly FunctionProbesManager? _probeManager;
        private readonly ParameterCapturingEventSource _eventSource = new();
        private readonly ILogger? _logger;


        private Channel<StartCapturingParametersPayload>? _requests;

        public ParameterCapturingService(IServiceProvider services)
        {
            _logger = services.GetService<ILogger<ParameterCapturingService>>();
            if (_logger == null)
            {
                return;
            }

            try
            {
                if (SharedInternals.MessageDispatcher == null)
                {
                    throw new NullReferenceException();
                }

                SharedInternals.MessageDispatcher.RegisterCallback<StartCapturingParametersPayload>(
                    IpcCommand.StartCapturingParameters,
                    OnStartMessage);

                SharedInternals.MessageDispatcher.RegisterCallback<EmptyPayload>(
                    IpcCommand.StopCapturingParameters,
                    OnStopMessage);

                _requests = Channel.CreateBounded<StartCapturingParametersPayload>(new BoundedChannelOptions(1)
                {
                    FullMode = BoundedChannelFullMode.Wait
                });

                _probeManager = new FunctionProbesManager(new LogEmittingProbes(_logger, FunctionProbesStub.InstrumentedMethodCache));
                _isAvailable = true;
            }
            catch
            {
                // TODO: Log
            }
        }

        private void OnStartMessage(StartCapturingParametersPayload payload)
        {
            if (payload.Methods.Length == 0)
            {
                return;
            }

            _ = _requests?.Writer.TryWrite(payload);
        }

        private void OnStopMessage(EmptyPayload _)
        {
            StopCore();
        }

        private void StartCore(StartCapturingParametersPayload request)
        {
            if (!_isAvailable || _probeManager == null)
            {
                return;
            }

            Assembly[] assemblies = AppDomain.CurrentDomain.GetAssemblies().Where(
     assembly => !assembly.ReflectionOnly && !assembly.IsDynamic).ToArray();

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

            List<MethodInfo> methods = new(request.Methods.Length);
            List<int> unableToResolve = new();
            for (int i = 0; i < request.Methods.Length; i++)
            {
                MethodDescription methodDescription = request.Methods[i];

                List<MethodInfo> resolvedMethods = ResolveMethod(dllNameToModules, methodDescription);
                if (resolvedMethods.Count == 0)
                {
                    _logger?.LogWarning(ParameterCapturingStrings.UnableToResolveMethod, methodDescription);
                    unableToResolve.Add(i);
                }

                methods.AddRange(resolvedMethods);
            }

            if (unableToResolve.Count > 0)
            {
                _eventSource.UnableToResolveMethods(unableToResolve.ToArray());
                return;
            }


            if (Interlocked.CompareExchange(ref _activeRequests, 1, 0) == 0)
            {
                _logger?.LogInformation(ParameterCapturingStrings.StartParameterCapturing, string.Join(' ', request.Methods));
                _probeManager.StartCapturing(methods);
                _eventSource.StartedCapturing();
            }
        }

        private void StopCore()
        {
            if (!_isAvailable || _probeManager == null)
            {
                return;
            }

            if (Interlocked.CompareExchange(ref _activeRequests, 0, 1) == 1)
            {
                _logger?.LogInformation(ParameterCapturingStrings.StopParameterCapturing);
                _probeManager.StopCapturing();
                _eventSource.StoppedCapturing();
            }
        }

        private List<MethodInfo> ResolveMethod(Dictionary<string, List<Module>> dllNameToModules, MethodDescription methodDescription)
        {
            List<MethodInfo> methods = new();

            if (!dllNameToModules.TryGetValue(methodDescription.ModuleName, out List<Module>? possibleModules))
            {
                return methods;
            }

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

                    foreach (MethodInfo method in allMethods)
                    {
                        if (method.Name != methodDescription.MethodName)
                        {
                            continue;
                        }

                        if (!methodDescription.FilterByParameters)
                        {
                            methods.Add(method);
                            continue;
                        }

                        ParameterInfo[] paramInfo = method.GetParameters();
                        if (paramInfo.Length != methodDescription.ParameterTypes?.Length)
                        {
                            continue;
                        }

                        bool mismatch = false;
                        for (int i = 0; i < paramInfo.Length; i++)
                        {
                            if (paramInfo[i].ParameterType.Name != methodDescription.ParameterTypes[i])
                            {
                                mismatch = true;
                                break;
                            }
                        }

                        if (!mismatch)
                        {
                            methods.Add(method);
                        }
                    }
                }
                catch (Exception ex)
                {
                    _logger?.LogWarning($"Unable resolve method {methodDescription}, exception: {ex}");
                }
            }

            return methods;
        }

        protected override async Task ExecuteAsync(CancellationToken stoppingToken)
        {
            if (!_isAvailable)
            {
                return;
            }

            stoppingToken.Register(StopCore);
            while (!stoppingToken.IsCancellationRequested)
            {
                StartCapturingParametersPayload req = await _requests!.Reader.ReadAsync(stoppingToken);
                StartCore(req);
                await Task.Delay(req.Duration, stoppingToken).ConfigureAwait(false);
                StopCore();
            }
        }

        public override void Dispose()
        {
            if (!DisposableHelper.CanDispose(ref _disposedState))
                return;

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
