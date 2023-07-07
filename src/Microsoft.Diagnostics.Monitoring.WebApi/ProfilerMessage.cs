// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Text.Json;

namespace Microsoft.Diagnostics.Monitoring
{
    internal sealed class EmptyPayload { }

    internal struct MethodDescription
    {
        public MethodDescription()
        {
            ModuleName = string.Empty;
            ClassName = string.Empty;
            MethodName = string.Empty;
            FilterByParameters = false;
            ParameterTypes = Array.Empty<string>();
        }

        public MethodDescription(string fqMethodName)
        {
            // JSFIX: proof-of-concept code
            int dllSplitIndex = fqMethodName.IndexOf('!');
            string dll = fqMethodName[..dllSplitIndex];
            string classAndMethod = fqMethodName[(dllSplitIndex + 1)..];

            int lastIndex = classAndMethod.LastIndexOf('.');

            string className = classAndMethod[..lastIndex];
            string methodNameWithParameters = classAndMethod[(lastIndex + 1)..];
            if (methodNameWithParameters == null)
            {
                throw new ArgumentException();
            }

            int paramStartIndex = methodNameWithParameters.IndexOf('(');
            string methodName;
            List<string> parameterTypes = new();
            if (paramStartIndex == -1)
            {
                methodName = methodNameWithParameters;
            }
            else
            {
                methodName = methodNameWithParameters[..paramStartIndex];
                int paramEndIndex = methodNameWithParameters.IndexOf(')');
                string typeInfo = methodNameWithParameters[(paramStartIndex + 1)..paramEndIndex];
                if (typeInfo.Length == 0)
                {
                    parameterTypes = new List<string>(0);
                }
                else
                {
                    parameterTypes = typeInfo.Split(',').ToList();
                }
            }

            ModuleName = dll;
            ClassName = className;
            MethodName = methodName;
            FilterByParameters = parameterTypes.Count != 0;
            ParameterTypes = parameterTypes?.ToArray() ?? Array.Empty<string>();
        }

        public string ModuleName { get; set; }
        public string ClassName { get; set; }
        public string MethodName { get; set; }

        public bool FilterByParameters { get; set; }

        public string[] ParameterTypes { get; set; }

        public override string ToString()
        {
            if (!FilterByParameters || ParameterTypes == null)
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
