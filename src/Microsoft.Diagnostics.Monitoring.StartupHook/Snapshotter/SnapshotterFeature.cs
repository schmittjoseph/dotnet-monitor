// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

namespace Microsoft.Diagnostics.Monitoring.StartupHook.Snapshotter
{
    internal sealed class SnapshotterFeature : AbstractInProcFeature
    {
        public SnapshotterFeature()
        {
        }

        protected override void DoInit()
        {
            // The snapshotter feature is entirely handled in the HostingStartup assembly, ensure we attempt to load it.
            AspNetHostingStartupHelper.RegisterHostingStartup();
        }
    }
}
