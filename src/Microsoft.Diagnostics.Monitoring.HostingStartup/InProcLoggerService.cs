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
        public static bool IsAvailable { get; private set; }

        public InProcLoggerService(IServiceProvider services)
        {
            _logger = services.GetService<ILogger<InProcLoggerService>>();
            if (_logger != null)
            {
                IsAvailable = true;
            }
        }

        public static void Log(string message, LogLevel level = LogLevel.Critical)
        {
            _logger?.Log(level, message);
        }

        protected override async Task ExecuteAsync(CancellationToken stoppingToken)
        {
            await Task.Delay(Timeout.Infinite, stoppingToken);
        }
    }
}
