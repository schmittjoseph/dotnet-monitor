// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using System;
using System.Text;
using System.Text.Json;

namespace Microsoft.Diagnostics.Monitoring
{
    internal enum ProfilerPayloadType : short
    {
        None,
        Int32Parameter, // Payload only contains an INT32
        Utf8Json // Payload contains a UTF8-encoded JSON string
    };

    internal enum ProfilerMessageType : short
    {
        Unknown,
        Status,
        Callstack
    };

    internal interface IProfilerMessage
    {
        public ProfilerPayloadType PayloadType { get; set; }
        public ProfilerMessageType MessageType { get; set; }

        public byte[] SerializePayload();
    }

    internal struct JsonProfilerMessage : IProfilerMessage
    {
        public ProfilerPayloadType PayloadType { get; set; } = ProfilerPayloadType.Utf8Json;
        public ProfilerMessageType MessageType { get; set; } = ProfilerMessageType.Unknown;

        public object Payload { get; set; }

        public JsonProfilerMessage(ProfilerMessageType messageType, object t)
        {
            MessageType = messageType;
            Payload = t;
        }

        public byte[] SerializePayload()
        {
            string jsonPayload = JsonSerializer.Serialize(Payload);
            return Encoding.UTF8.GetBytes(jsonPayload);
        }
    }

    internal struct HeaderOnlyProfilerMessage : IProfilerMessage
    {
        public HeaderOnlyProfilerMessage(ProfilerMessageType messageType)
        {
            MessageType = messageType;
        }

        public ProfilerPayloadType PayloadType { get; set; } = ProfilerPayloadType.None;
        public ProfilerMessageType MessageType { get; set; } = ProfilerMessageType.Unknown;

        public byte[] SerializePayload()
        {
            return Array.Empty<byte>();
        }
    }

}
