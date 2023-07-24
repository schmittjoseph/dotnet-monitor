// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using Microsoft.Diagnostics.Monitoring.HostingStartup.ParameterCapturing.FunctionProbes;
using Microsoft.Diagnostics.Monitoring.StartupHook;
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
                _probeManager = new FunctionProbesManager(new LogEmittingProbes(_logger));
                _isAvailable = true;
            }
            catch
            {
                // TODO: Log
            }
        }

        public void StopCapturing()
        {
            if (!_isAvailable)
            {
                throw new InvalidOperationException();
            }

            _probeManager?.StopCapturing();
        }

        public void StartCapturing(IList<MethodInfo> methods)
        {
            if (!_isAvailable)
            {
                throw new InvalidOperationException();
            }

            _probeManager?.StartCapturing(methods);
        }

        protected override Task ExecuteAsync(CancellationToken stoppingToken)
        {
            if (!_isAvailable)
            {
                return Task.CompletedTask;
            }

            /*
            if (!Debugger.IsAttached)
            {
                Debugger.Launch();
            }
            */

            StressTest();

            return Task.Delay(Timeout.Infinite, stoppingToken);
        }


        private void StressTest()
        {
            const bool OnlyHookCorLib = true;

            List<MethodInfo> methods = new();
            Assembly[] assemblies = AppDomain.CurrentDomain.GetAssemblies().Where(assembly => !assembly.ReflectionOnly && !assembly.IsDynamic).ToArray();
            foreach (Assembly assembly in assemblies)
            {
                foreach (Module mod in assembly.Modules)
                {
                    if (OnlyHookCorLib && !mod.Name.StartsWith("System.Private.CoreLib", StringComparison.OrdinalIgnoreCase))
                    {
                        continue;
                    }

                    foreach (Type type in mod.GetTypes())
                    {
                        methods.AddRange(GetAllMethods(type));
                    }
                }
            }

            _logger!.LogCritical("Beginning stress test, hooked {Number} methods", methods.Count);
            _probeManager!.StartCapturing(methods);
        }


        private static List<MethodInfo> GetAllMethods(Type containingType)
        {
            List<MethodInfo> methods = new();

            MethodInfo[] possibleMethods = containingType.GetMethods(
                BindingFlags.Public |
                BindingFlags.NonPublic |
                BindingFlags.Instance |
                BindingFlags.Static);

            foreach (MethodInfo method in possibleMethods)
            {
                Type? declType = method.DeclaringType;
                if (declType == null)
                {
                    continue;
                }

                if (declType.IsConstructedGenericType)
                {
                    continue;
                }

                string fullName = $"{declType.Module.Name}!{declType.FullName}.{method.Name}";

                if (fullName.Contains("System.Diagnostics.") ||
//                    fullName.Contains("log", StringComparison.OrdinalIgnoreCase) ||
//                    fullName.Contains("System.Collections") ||
//                    fullName.Contains("System.Threading") ||
                    fullName.Contains("Microsoft.Diagnostics.") ||
                    false
                    )
                {
                    continue;
                }

                methods.Add(method);
            }

            /*
            foreach (var nestedType in containingType.GetNestedTypes())
            {
                methods.AddRange(GetAllMethods(nestedType));
            }
            */

            return methods;
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
