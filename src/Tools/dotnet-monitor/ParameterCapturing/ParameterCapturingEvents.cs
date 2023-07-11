// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

namespace Microsoft.Diagnostics.Tools.Monitor.ParameterCapturing
{
    internal static class ParameterCapturingEvents
    {
        public const string SourceName = "Microsoft.Diagnostics.Monitoring.ParameterCapturing";

        public static class EventIds
        {
            // 1 Flush
            public const int StartedCapturing = 2;
            public const int StoppedCapturing = 3;
            public const int Error = 4;

            public const int UnableToResolveMethods = 5;
        }

        public static class UnableToResolveMethodsPayloads
        {
            public const int MethodDescriptionIndices = 0;
        }
    }
}
