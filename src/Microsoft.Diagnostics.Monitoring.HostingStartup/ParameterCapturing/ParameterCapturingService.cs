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

            if (payload.Methods.Length == 0)
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

            List<MethodInfo> methods = new(payload.Methods.Length);
            foreach (MethodDescription methodDescription in payload.Methods)
            {
                List<MethodInfo> resolvedMethods = ResolveMethod(dllNameToModules, methodDescription);
                if (resolvedMethods.Count == 0)
                {
                    _logger?.LogWarning(ParameterCapturingStrings.UnableToResolveMethod, methodDescription);
                    throw new ArgumentException($"Failed to resolve method: {methodDescription}");
                }

                methods.AddRange(resolvedMethods);
            }

            _logger?.LogInformation(ParameterCapturingStrings.StartParameterCapturing, string.Join(' ', payload.Methods));
            _probeManager.StartCapturing(methods);
        }

        private void OnStopMessage(EmptyPayload _)
        {
            if (!_isAvailable || _probeManager == null)
            {
                return;
            }

            _logger?.LogInformation(ParameterCapturingStrings.StopParameterCapturing);
            _probeManager.StopCapturing();
        }

        private static MethodDescription? ExtractMethodInfo(string fqMethodName)
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

            return new MethodDescription
            {
                ModuleName = dll,
                ClassName = className,
                MethodName = methodName,
                FilterByParameters = parameterTypes != null,
                ParameterTypes = parameterTypes?.ToArray() ?? Array.Empty<string>()
            };
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

                        if (methodDescription.ParameterTypes == null)
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

        protected override Task ExecuteAsync(CancellationToken stoppingToken)
        {
            if (!_isAvailable)
            {
                return Task.CompletedTask;
            }

            return Task.Delay(Timeout.Infinite, stoppingToken);
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
