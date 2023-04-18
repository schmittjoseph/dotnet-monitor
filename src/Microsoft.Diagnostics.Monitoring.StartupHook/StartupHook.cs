// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using Microsoft.Diagnostics.Monitoring.StartupHook.Exceptions;
using Microsoft.Diagnostics.Monitoring.StartupHook.Snapshotter;

// [assembly: HostingStartup(typeof(InProcLogger))]

internal sealed class StartupHook
{
    public static void Initialize()
    {
        AbstractInProcFeature[] features = new AbstractInProcFeature[] { new ExceptionsFeature(), new SnapshotterFeature() };

        // Request aspnet to load our startup assembly for in-proc-logging.

        foreach (AbstractInProcFeature feature in features)
        {
            feature.TryInit();
        }
    }
}
