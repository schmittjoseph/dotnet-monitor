// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using Microsoft.Diagnostics.Monitoring.StartupHook.Eventing;
using Microsoft.Diagnostics.Tools.Monitor.ParameterCapturing;
using System;
using System.Diagnostics.Tracing;

namespace Microsoft.Diagnostics.Monitoring.HostingStartup.ParameterCapturing.Eventing
{
    // This event source should be optimized for speed as much as possible since it will
    // likely be sending many events every second. Avoid any APIs that use params/varargs
    // style calls and avoid heap allocations as much as possible.
    [EventSource(Name = ParameterCapturingEvents.SourceName)]
    internal sealed class ParameterCapturingEventSource : AbstractMonitorEventSource
    {
        [Event(ParameterCapturingEvents.EventIds.CapturingStart)]
        public void CapturingStart()
        {
            
            WriteEventWithFlushing(ParameterCapturingEvents.EventIds.CapturingStart);
        }

        [Event(ParameterCapturingEvents.EventIds.CapturingStop)]
        public void CapturingStop()
        {
            WriteEventWithFlushing(ParameterCapturingEvents.EventIds.CapturingStop);
        }

        [NonEvent]
        public void FailedToCapture(Exception ex)
        {
            FailedToCapture(ex.GetType().FullName ?? string.Empty, ex.ToString());
        }

        [Event(ParameterCapturingEvents.EventIds.FailedToCapture)]
        public void FailedToCapture(
            string exceptionType,
            string exceptionMessage)
        {
            Span<EventData> data = stackalloc EventData[2];
            using PinnedData typePinned = PinnedData.Create(exceptionType);
            using PinnedData messagePinned = PinnedData.Create(exceptionMessage);

            SetValue(ref data[ParameterCapturingEvents.CapturingFailedPayloads.FailureType], typePinned);
            SetValue(ref data[ParameterCapturingEvents.CapturingFailedPayloads.FailureMessage], messagePinned);

            WriteEventWithFlushing(ParameterCapturingEvents.EventIds.FailedToCapture, data);
        }

        [Event(ParameterCapturingEvents.EventIds.Flush)]
        protected override void Flush()
        {
            WriteEvent(ParameterCapturingEvents.EventIds.Flush);
        }
    }
}
