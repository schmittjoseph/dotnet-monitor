﻿// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using System;
using System.Text;
using System.Text.Json;

namespace Microsoft.Diagnostics.Monitoring
{
    internal sealed class EmptyPayload { }

    internal struct MethodDescription
    {
        public string ModuleName { get; set; }
        public string ClassName { get; set; }
        public string MethodName { get; set; }

        public bool FilterByParameters { get; set; }

        public string[] ParameterTypes { get; set; }

        public override string ToString()
        {
            if (FilterByParameters)
            {
                return FormattableString.Invariant($"{ModuleName}!{ClassName}.{MethodName}");
            }
            else
            {
                return FormattableString.Invariant($"{ModuleName}!{ClassName}.{MethodName}({string.Join(", ", ParameterTypes)})");
            }
        }
    }

    internal sealed class StartCapturingParametersPayload
    {
        public MethodDescription[] Methods { get; set; } = Array.Empty<MethodDescription>();
    }

    internal enum ProfilerPayloadType : short
    {
        None,
        Utf8Json
    };

    internal enum ProfilerMessageType : short
    {
        Unknown,
        Status,
        Callstack,
        StartCapturingParameters,
        StopCapturingParameters
    };

    internal interface IProfilerMessage
    {
        public ProfilerPayloadType PayloadType { get; set; }
        public ProfilerMessageType MessageType { get; set; }
        public int Parameter { get; set; }
        public byte[] Payload { get; set; }
    }

    internal struct JsonProfilerMessage : IProfilerMessage
    {
        public ProfilerPayloadType PayloadType { get; set; } = ProfilerPayloadType.Utf8Json;
        public ProfilerMessageType MessageType { get; set; } = ProfilerMessageType.Unknown;
        public int Parameter { get; set; }
        public byte[] Payload { get; set; }

        public JsonProfilerMessage(ProfilerMessageType messageType, object payloadObject)
        {
            MessageType = messageType;
            Payload = SerializePayload(payloadObject);
            Parameter = Payload.Length;
        }

        private static byte[] SerializePayload(object payloadObject)
        {
            string jsonPayload = JsonSerializer.Serialize(payloadObject);
            return Encoding.UTF8.GetBytes(jsonPayload);
        }
    }

    internal struct BasicProfilerMessage : IProfilerMessage
    {
        public ProfilerPayloadType PayloadType { get; set; } = ProfilerPayloadType.None;
        public ProfilerMessageType MessageType { get; set; } = ProfilerMessageType.Unknown;
        public int Parameter { get; set; } = 0;
        public byte[] Payload { get; set; } = Array.Empty<byte>();

        public BasicProfilerMessage(ProfilerMessageType messageType, int parameter = 0)
        {
            MessageType = messageType;
            Parameter = parameter;
        }
    }
}
