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
            AspNetHostingStartupHelper.RegisterHostingStartup();
        }


        protected override string Name()
        {
            return "Snapshotter";
        }
    }
}
