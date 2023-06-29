// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using Microsoft.Diagnostics.Monitoring.HostingStartup.ParameterCapturing.FunctionProbes;
using Microsoft.Diagnostics.Monitoring.StartupHook;
using Microsoft.Diagnostics.Tools.Monitor.StartupHook;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.ObjectPool;
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

        private struct ResolveMethodInfo
        {
            public string ModuleName { get; set; }
            public string ClassName { get; set; }
            public string MethodName { get; set; }
            public string[]? ParameterTypes { get; set; }

            public ulong? FunctionId { get; set; }
        }

        private static ResolveMethodInfo? ExtractMethodInfo(string fqMethodName)
        {
            // JSFIX: proof-of-concept code
            int dllSplitIndex = fqMethodName.IndexOf('!');
            string dll = fqMethodName[..dllSplitIndex];
            string classAndMethod = fqMethodName[(dllSplitIndex + 1)..];

            int lastIndex = classAndMethod.LastIndexOf('.');

            string className = classAndMethod[..lastIndex];
            string methodNameWithParameters = classAndMethod[(lastIndex + 1)..];
            if (methodNameWithParameters == null)
            {
                return null;
            }

            int paramStartIndex = methodNameWithParameters.IndexOf('(');
            string methodName;
            List<string>? parameterTypes = null;
            if (paramStartIndex == -1)
            {
                methodName = methodNameWithParameters;
            }
            else
            {
                methodName = methodNameWithParameters[..paramStartIndex];
                int paramEndIndex = methodNameWithParameters.IndexOf(')');
                string typeInfo = methodNameWithParameters[(paramStartIndex + 1)..paramEndIndex];
                if (typeInfo.Length == 0)
                {
                    parameterTypes = new List<string>(0);
                }
                else
                {
                    parameterTypes = typeInfo.Split(',').ToList();
                }
            }

            return new ResolveMethodInfo
            {
                ModuleName = dll,
                ClassName = className,
                MethodName = methodName,
                ParameterTypes = parameterTypes?.ToArray()
            };
        }

        private List<MethodInfo> ResolveMethod(Dictionary<string, List<Module>> dllNameToModules, string fqMethodName)
        {
            List<MethodInfo> methods = new();

            ResolveMethodInfo? resolvedInfoN = ExtractMethodInfo(fqMethodName);
            if (resolvedInfoN == null)
            {
                return methods;
            }

            ResolveMethodInfo resolvedInfo = resolvedInfoN.Value;

            if (!dllNameToModules.TryGetValue(resolvedInfo.ModuleName, out List<Module>? possibleModules))
            {
                return methods;
            }

            foreach (Module module in possibleModules)
            {
                try
                {
                    MethodInfo[]? allMethods = module.Assembly.GetType(resolvedInfo.ClassName)?.GetMethods(BindingFlags.Public |
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
                        if (method.Name != resolvedInfo.MethodName)
                        {
                            continue;
                        }

                        if (resolvedInfo.ParameterTypes == null)
                        {
                            methods.Add(method);
                            continue;
                        }

                        ParameterInfo[] paramInfo = method.GetParameters();
                        if (paramInfo.Length != resolvedInfo.ParameterTypes?.Length)
                        {
                            continue;
                        }

                        bool mismatch = false;
                        for (int i = 0; i < paramInfo.Length; i++)
                        {
                            if (paramInfo[i].ParameterType.Name != resolvedInfo.ParameterTypes[i])
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

                    /*
                    if (!resolvedInfo.FunctionId.HasValue ||
                         resolvedInfo.FunctionId.Value == method.GetFunctionId())
                    {
                        methods.Add(method);
                    }
                    */

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
