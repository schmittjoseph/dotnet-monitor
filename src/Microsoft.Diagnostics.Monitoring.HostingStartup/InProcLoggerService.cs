// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using System;
using System.Threading;
using System.Threading.Tasks;

namespace Microsoft.Diagnostics.Monitoring.HostingStartup
{
    internal sealed class InProcLoggerService : BackgroundService
    {
        private static ILogger? _logger;
        private static ManualResetEventSlim _loggerReady = new(initialState: false);

        public InProcLoggerService(IServiceProvider services)
        {

            ILogger? logger = services.GetService<ILogger<InProcLoggerService>>();

            if (logger != null)
            {
                _logger = logger;
                _loggerReady.Set();
            }
            Log("Ready", LogLevel.Warning);
        }

        public static void Log(string message, LogLevel level = LogLevel.Critical)
        {
            if (_loggerReady.IsSet && _logger != null)
            {
                _logger.Log(level, message);
            }
            else
            {
                Console.WriteLine($"[in-proc-logger] {level}: {message}");
            }
        }

        protected override async Task ExecuteAsync(CancellationToken stoppingToken)
        {
            await Task.Delay(Timeout.Infinite, stoppingToken);
        }
    }
}
