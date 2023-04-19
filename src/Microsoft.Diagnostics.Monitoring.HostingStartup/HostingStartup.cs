﻿// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using Microsoft.AspNetCore.Hosting;
using Microsoft.Diagnostics.Monitoring.HostingStartup;
using Microsoft.Diagnostics.Monitoring.StartupHook.Snapshotter;
using Microsoft.Extensions.DependencyInjection;
using System;

[assembly: HostingStartup(typeof(HostingStartup))]
namespace Microsoft.Diagnostics.Monitoring.HostingStartup
{
    internal sealed class HostingStartup : IHostingStartup
    {
        public void Configure(IWebHostBuilder builder)
        {
            SnapshotterFeature snapshotterFeature = new();

            snapshotterFeature.DoInit();

            Console.WriteLine("[hosting-startup]dbug: Configuring.");
            builder.ConfigureServices(services =>
            {
                services.AddHostedService<InProcLoggerService>();
            });
        }
    }
}
