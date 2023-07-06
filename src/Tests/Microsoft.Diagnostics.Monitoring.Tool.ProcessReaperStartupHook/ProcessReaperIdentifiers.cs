// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using Microsoft.Diagnostics.Tools.Monitor;

namespace Microsoft.Diagnostics.Monitoring.Tool.ProcessReaper
{
    public static class ProcessReaperIdentifiers
    {
        public static class EnvironmentVariables
        {
            private const string ProcessReaperPrefix = ToolIdentifiers.StandardPrefix + "TestProcessReaper_";

            public const string Passthrough = ProcessReaperPrefix + nameof(Passthrough);
        }
    }
}
