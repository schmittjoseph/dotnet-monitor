// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using System;
using System.Text;
using System.Text.Json;
using System.Threading;

namespace Microsoft.Diagnostics.Monitoring
{
    internal sealed class EmptyPayload { }

    internal struct MethodDescription
    {
        public MethodDescription() { }

        public MethodDescription(string fqMethodName)
        {
            // JSFIX: proof-of-concept code
            int dllSplitIndex = fqMethodName.IndexOf('!');
            string dll = fqMethodName[..dllSplitIndex];
            string classAndMethod = fqMethodName[(dllSplitIndex + 1)..];

            int lastIndex = classAndMethod.LastIndexOf('.');

            string className = classAndMethod[..lastIndex];
            string methodName = classAndMethod[(lastIndex + 1)..];
            if (methodName == null)
            {
                throw new ArgumentException();
            }

            ModuleName = dll;
            ClassName = className;
            MethodName = methodName;
        }

        public string ModuleName { get; set; } = string.Empty;
        public string ClassName { get; set; } = string.Empty;
        public string MethodName { get; set; } = string.Empty;

        public override string ToString() => FormattableString.Invariant($"{ModuleName}!{ClassName}.{MethodName}");
    }

    internal sealed class StartCapturingParametersPayload
    {
        public TimeSpan Duration { get; set; } = Timeout.InfiniteTimeSpan;
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
        public IpcCommand Command { get; }
        public byte[] Payload { get; }
    }

    internal struct JsonProfilerMessage : IProfilerMessage
    {
        public IpcCommand Command { get; }
        public byte[] Payload { get; }

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
        public IpcCommand Command { get; }
        public byte[] Payload { get; } = Array.Empty<byte>();

        public CommandOnlyProfilerMessage(IpcCommand command)
        {
            Command = command;
        }
    }
}
