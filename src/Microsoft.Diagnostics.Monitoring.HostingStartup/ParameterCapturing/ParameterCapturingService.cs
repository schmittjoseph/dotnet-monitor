// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

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
using System.Threading.Tasks;

namespace Microsoft.Diagnostics.Monitoring.HostingStartup.ParameterCapturing
{
    internal sealed class ParameterCapturingService : BackgroundService, IDisposable
    {
        private long _disposedState;
        private readonly bool _isAvailable;

        private readonly FunctionProbesManager? _probeManager;
        private readonly ILogger? _logger;

        private CancellationTokenSource? _stopCapturingSource;
        private readonly SemaphoreSlim _semaphore = new(1);

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
                    ProfilerMessageType.StartCapturingParameters,
                    OnStartMessage);

                SharedInternals.MessageDispatcher.RegisterCallback<EmptyPayload>(
                    ProfilerMessageType.StopCapturingParameters,
                    (_) => _stopCapturingSource?.Cancel());

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
            if (!_isAvailable || _probeManager == null)
            {
                return;
            }

            if (payload.MethodNames.Length == 0 || payload.Duration == TimeSpan.Zero)
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

            List<MethodInfo> methods = new(payload.MethodNames.Length);
            foreach (string methodName in payload.MethodNames)
            {
                List<MethodInfo> resolvedMethods = ResolveMethod(dllNameToModules, methodName);
                if (resolvedMethods.Count == 0)
                {
                    _logger?.LogWarning(ParameterCapturingStrings.UnableToResolveMethod, methodName);
                    throw new ArgumentException($"Failed to resolve method: {methodName}");
                }

                methods.AddRange(resolvedMethods);
            }

            _logger?.LogInformation(ParameterCapturingStrings.StartParameterCapturing, payload.Duration, string.Join(' ', payload.MethodNames));
            _probeManager.StartCapturing(methods);

            _stopCapturingSource = new(payload.Duration);
        }

        private void StopCapturing()
        {
            if (!_isAvailable || _probeManager == null)
            {
                return;
            }

            _logger?.LogInformation(ParameterCapturingStrings.StopParameterCapturing);
            _probeManager.StopCapturing();
        }

        private List<MethodInfo> ResolveMethod(Dictionary<string, List<Module>> dllNameToModules, string fqMethodName)
        {
            // JSFIX: proof-of-concept code
            int dllSplitIndex = fqMethodName.IndexOf('!');
            string dll = fqMethodName[..dllSplitIndex];
            string classAndMethod = fqMethodName[(dllSplitIndex + 1)..];

            int lastIndex = classAndMethod.LastIndexOf('.');

            string className = classAndMethod[..lastIndex];
            string methodName = classAndMethod[(lastIndex + 1)..];

            List<MethodInfo> methods = new();

            if (!dllNameToModules.TryGetValue(dll, out List<Module>? possibleModules))
            {
                return methods;
            }

            foreach (Module module in possibleModules)
            {
                try
                {
                    MethodInfo? method = module.Assembly.GetType(className)?.GetMethod(methodName,
                        BindingFlags.Public |
                        BindingFlags.NonPublic |
                        BindingFlags.Instance |
                        BindingFlags.Static |
                        BindingFlags.FlattenHierarchy);

                    if (method != null)
                    {
                        methods.Add(method);
                        continue;
                    }
                }
                catch (Exception ex)
                {
                    _logger?.LogWarning($"Unable resolve method {fqMethodName}, exception: {ex}");
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

            while (!stoppingToken.IsCancellationRequested)
            {
                await _semaphore.WaitAsync(stoppingToken).ConfigureAwait(false);
                CancellationToken stopCapToken = _stopCapturingSource?.Token ?? throw new Exception();
                using CancellationTokenSource combinedCancellation = CancellationTokenSource.CreateLinkedTokenSource(stoppingToken, stopCapToken);
                await Task.Delay(Timeout.Infinite, combinedCancellation).ConfigureAwait(false);

                if (stopCapToken.IsCancellationRequested)
                {

                }
                StopCapturing();
            }
        }

        public override void Dispose()
        {
            if (!DisposableHelper.CanDispose(ref _disposedState))
                return;

            _semaphore.Dispose();
            _stopCapturingSource?.Dispose();

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
