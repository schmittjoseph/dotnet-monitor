// Licensed to the .NET Foundation under one or more agreements.
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

    internal enum IpcCommand : short
    {
        Unknown,
        Status,
        Callstack,
        StartCapturingParameters,
        StopCapturingParameters
    };

    internal interface IProfilerMessage
    {
        public IpcCommand Command { get; set; }
        public byte[] Payload { get; set; }
    }

    internal struct JsonProfilerMessage : IProfilerMessage
    {
        public IpcCommand Command { get; set; } = IpcCommand.Unknown;
        public byte[] Payload { get; set; }

        public JsonProfilerMessage(IpcCommand command, object payloadObject)
        {
            Command = command;
            Payload = SerializePayload(payloadObject);
        }

        private static byte[] SerializePayload(object payloadObject)
        {
            string jsonPayload = JsonSerializer.Serialize(payloadObject);
            return Encoding.UTF8.GetBytes(jsonPayload);
        }
    }

    internal struct CommandOnlyProfilerMessage : IProfilerMessage
    {
        public IpcCommand Command { get; set; } = IpcCommand.Unknown;
        public byte[] Payload { get; set; } = Array.Empty<byte>();

        public CommandOnlyProfilerMessage(IpcCommand command)
        {
            Command = command;
        }
    }
}
