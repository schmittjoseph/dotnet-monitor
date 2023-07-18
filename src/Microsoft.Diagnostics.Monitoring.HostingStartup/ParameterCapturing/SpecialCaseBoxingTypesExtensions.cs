// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using static Microsoft.Diagnostics.Monitoring.HostingStartup.ParameterCapturing.BoxingTokensResolver;

namespace Microsoft.Diagnostics.Monitoring.HostingStartup.ParameterCapturing
{
    internal static class SpecialCaseBoxingTypesExtensions
    {
        public static uint BoxingToken(this SpecialCaseBoxingTypes specialCase)
        {
            const uint SpecialCaseBoxingTypeFlag = 0x7f000000;
            return SpecialCaseBoxingTypeFlag | (uint)specialCase;
        }
    }
}
