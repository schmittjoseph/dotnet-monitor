// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using System;
using System.Threading;

namespace Microsoft.Diagnostics.Monitoring.StartupHook
{
    internal sealed class InProcLogger
    {
        public enum LogLevel
        {
            Trace = 0,
            Debug = 1,
            Information = 2,
            Warning = 3,
            Error = 4,
            Critical = 5,
            None = 6,
        }

        // private static ILogger _logger;
        private static ManualResetEventSlim _loggerReady = new(initialState: false);

        /*
        public InProcLogger(IServiceProvider services)
        {
            ILogger logger = services.GetService<ILogger<InProcLogger>>();
            if (logger != null)
            {
                _logger = logger;
                _loggerReady.Set();
            }
        }
        */

        public static void Log(string message, LogLevel level = LogLevel.Critical)
        {
            if (_loggerReady.IsSet)
            {
                // _logger.Log(level, message);
            } else
            {
                Console.Error.WriteLine($"[startup-hook] {level}: {message}");
            }
        }
    }
}
