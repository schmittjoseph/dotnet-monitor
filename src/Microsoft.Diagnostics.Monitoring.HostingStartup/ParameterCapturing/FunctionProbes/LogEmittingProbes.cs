// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using Microsoft.Extensions.Logging;
using System.Diagnostics;

namespace Microsoft.Diagnostics.Monitoring.HostingStartup.ParameterCapturing.FunctionProbes
{
    internal sealed class LogEmittingProbes : IFunctionProbes
    {
        private readonly ILogger _logger;

        public LogEmittingProbes(ILogger logger)
        {
            _logger = logger;
        }

        public void EnterProbe(ulong uniquifier, object[] args)
        {
            // JSFIX: Expensive test code to avoid recursing on any methods called by loggers
            /*
            var trace = new StackTrace(fNeedFileInfo: false);
            foreach (var frame in trace.GetFrames())
            {
                if (frame.HasMethod() && frame.GetMethod()?.DeclaringType?.FullName?.Contains("log", System.StringComparison.OrdinalIgnoreCase) == true)
                {
                    return;
                }
            }
            */


            var methodCache = FunctionProbesStub.InstrumentedMethodCache;
            if (methodCache == null ||
                args == null ||
                !methodCache.TryGetValue(uniquifier, out InstrumentedMethod? instrumentedMethod) ||
                args.Length != instrumentedMethod?.SupportedParameters.Length)
            {
                return;
            }

            string[] argValues = new string[instrumentedMethod.NumberOfSupportedParameters];
            int fmtIndex = 0;
            for (int i = 0; i < args.Length; i++)
            {
                if (!instrumentedMethod.SupportedParameters[i])
                {
                    continue;
                }

                argValues[fmtIndex++] = PrettyPrinter.FormatObject(args[i]);
            }

            // Console.WriteLine("IO Test");
            _logger.Log(LogLevel.Information, instrumentedMethod.MethodWithParametersTemplateString, argValues);
            return;
        }
    }
}
