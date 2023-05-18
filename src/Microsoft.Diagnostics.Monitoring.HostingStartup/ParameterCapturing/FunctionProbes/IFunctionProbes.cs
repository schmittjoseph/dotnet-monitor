// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

namespace Microsoft.Diagnostics.Monitoring.HostingStartup.ParameterCapturing.FunctionProbes
{
    public delegate void EnterProbeDelegate(ulong uniquifier, object[] args);

    public interface IFunctionProbes
    {
        public ulong GetProbeFunctionId();
        public void EnterProbe(ulong uniquifier, object[] args);
    }
}
