// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

namespace Microsoft.Diagnostics.Monitoring.StartupHook.Exceptions
{
    internal sealed class ExceptionsFeature : AbstractInProcFeature
    {
        private CurrentAppDomainExceptionProcessor exceptionProcessor = new ();

        protected override void DoInit()
        {
            exceptionProcessor.Start();
        }
    }
}
