// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

namespace Microsoft.Diagnostics.Tools.Monitor.ParameterCapturing
{
    internal static class ParameterCapturingEvents
    {
        public const string SourceName = "Microsoft.Diagnostics.Monitoring.ParameterCapturing";

        public static class EventIds
        {
            public const int Flush = 1;

            public const int CapturingStart = 2;
            public const int CapturingStop = 3;
            public const int FailedToCapture = 4;
        }

        public enum CapturingFailureReason : short
        {
            UnableToResolveMethods,
            InternalError
        }

        public static class CapturingFailedPayloads
        {
            public const int FailureType = 0;
            public const int FailureMessage = 1;
        }
    }
}
