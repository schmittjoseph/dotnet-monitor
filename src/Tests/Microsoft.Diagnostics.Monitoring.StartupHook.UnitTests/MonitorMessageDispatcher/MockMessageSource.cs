// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using System;

namespace Microsoft.Diagnostics.Monitoring.StartupHook.MonitorMessageDispatcher
{
    internal sealed class MockMessageSource : IMonitorMessageSource
    {
        public event IMonitorMessageSource.MonitorMessageHandler? MonitorMessageEvent;

        public void RaiseMessage(MonitorMessageArgs e)
        {
            MonitorMessageEvent?.Invoke(this, e);
        }

        public void RaiseMessage(ProfilerMessageType messageType, object payload)
        {
            JsonProfilerMessage jsonProfilerMessage = new(messageType, payload);

            byte[] serializedPayload = jsonProfilerMessage.SerializePayload();

            unsafe
            {
                fixed (byte* ptr = serializedPayload)
                {
                    RaiseMessage(new MonitorMessageArgs(
                        jsonProfilerMessage.PayloadType,
                        messageType,
                        new IntPtr(ptr),
                        serializedPayload.Length));
                }
            }

        }

        public void Dispose()
        {
        }
    }
}
