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
        [Event(ParameterCapturingEvents.EventIds.StartedCapturing)]
        public void CapturingStart()
        {
            
            WriteEventWithFlushing(ParameterCapturingEvents.EventIds.StartedCapturing);
        }

        [Event(ParameterCapturingEvents.EventIds.StoppedCapturing)]
        public void CapturingStop()
        {
            WriteEventWithFlushing(ParameterCapturingEvents.EventIds.StoppedCapturing);
        }

        [Event(ParameterCapturingEvents.EventIds.Error)]
        public void Error()
        {
            WriteEventWithFlushing(ParameterCapturingEvents.EventIds.Error);
        }

        [Event(ParameterCapturingEvents.EventIds.UnableToResolveMethods)]
        public void UnableToResolveMethods(int[] MethodDescriptionIndices)
        {
            Span<EventData> data = stackalloc EventData[1];
            Span<byte> methodDescriptionIndicesAsSpan = stackalloc byte[GetArrayDataSize(MethodDescriptionIndices)];
            FillArrayData(methodDescriptionIndicesAsSpan, MethodDescriptionIndices);

            SetValue(ref data[ParameterCapturingEvents.UnableToResolveMethodsPayloads.MethodDescriptionIndices], methodDescriptionIndicesAsSpan);

            WriteEventWithFlushing(ParameterCapturingEvents.EventIds.UnableToResolveMethods, data);
        }

        [Event(ParameterCapturingEvents.EventIds.Flush)]
        protected override void Flush()
        {
            WriteEvent(ParameterCapturingEvents.EventIds.Flush);
        }
    }
}
