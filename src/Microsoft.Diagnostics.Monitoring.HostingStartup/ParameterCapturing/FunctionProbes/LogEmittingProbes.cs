// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using Microsoft.Extensions.Logging;
using System.Text;

namespace Microsoft.Diagnostics.Monitoring.HostingStartup.ParameterCapturing.FunctionProbes
{
    // Must be public.
    public sealed class LogEmittingProbes : IFunctionProbes
    {
        private readonly InstrumentedMethodCache _methodCache;
        private readonly EnterProbeDelegate _pinnedProbeDelegate;
        private readonly ILogger _logger;

        public LogEmittingProbes(ILogger logger, InstrumentedMethodCache cache)
        {
            _logger = logger;
            _methodCache = cache;
            _pinnedProbeDelegate = EnterProbe;
        }

        public ulong GetProbeFunctionId()
        {
            return (ulong)_pinnedProbeDelegate.Method.MethodHandle.Value.ToInt64();
        }

        public void EnterProbe(ulong uniquifier, object[] args)
        {
            if (!_methodCache.TryGetValue(uniquifier, out InstrumentedMethod instrumentedMethod) ||
                args?.Length != instrumentedMethod.Parameters.Length)
            {
                return;
            }

            StringBuilder argBuilder = new();
            string[] argValues = new string[args.Length];
            for (int i = 0; i < args?.Length; i++)
            {
                if (!instrumentedMethod.SupportedArgs[i])
                {
                    continue;
                }

                PrettyPrinter.EmitSerializedObject(argBuilder, args[i]);
                argValues[i] = argBuilder.ToString();
                argBuilder.Clear();
            }

            _logger.LogInformation(instrumentedMethod.PrettyPrintStringFormat, argValues);
            return;
        }
    }
}
