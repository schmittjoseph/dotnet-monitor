// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using Microsoft.Extensions.Logging;
using System.Text;

namespace Microsoft.Diagnostics.Monitoring.HostingStartup.ParameterCapturing.FunctionProbes
{
    public static class LogEmittingProbes
    {
        public delegate void EnterProbeDelegate(ulong uniquifier, object[] args);

        private static InstrumentedMethodCache? s_methodCache;
        private static readonly EnterProbeDelegate s_pinnedProbeDelegate = EnterProbe;
        private static ILogger? s_logger;

        public static void Init(ILogger logger, InstrumentedMethodCache cache)
        {
            s_logger = logger;
            s_methodCache = cache;
        }

        public static ulong GetProbeFunctionId()
        {
            return (ulong)s_pinnedProbeDelegate.Method.MethodHandle.Value.ToInt64();
        }

        public static void EnterProbe(ulong uniquifier, object[] args)
        {
            if (s_logger == null ||
                s_methodCache?.TryGetValue(uniquifier, out InstrumentedMethod instrumentedMethod) != true ||
                args?.Length != instrumentedMethod.Parameters.Length)
            {
                return;
            }

            string[] argValues = new string[args.Length];
            for (int i = 0; i < args?.Length; i++)
            {
                if (!instrumentedMethod.SupportedArgs[i])
                {
                    continue;
                }
                
                argValues[i] = PrettyPrinter.SerializeObject(args[i]);
            }

            s_logger.LogInformation(instrumentedMethod.PrettyPrintStringFormat, argValues);
            return;
        }
    }
}
