// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using System;
using System.Collections.Concurrent;
using System.Reflection;

namespace Microsoft.Diagnostics.Monitoring.HostingStartup.ParameterCapturing
{
    public readonly struct InstrumentedMethod
    {
        public InstrumentedMethod(
            MethodInfo methodInfo,
            string prettyPrintStringFormat,
            bool[] supportedArgs,
            bool hasImplicitThis,
            Type? declaringType,
            ParameterInfo[] parameters)
        {
            MethodInfo = methodInfo;
            PrettyPrintStringFormat = prettyPrintStringFormat;

            SupportedArgs = supportedArgs;
            foreach (bool isArgSupported in supportedArgs)
            {
                if (isArgSupported)
                {
                    NumberOfSupportedArgs++;
                }
            }

            HasImplicitThis = hasImplicitThis;
            DeclaringType = declaringType;
            Parameters = parameters;
        }

        public MethodInfo MethodInfo { get; }
        public int NumberOfSupportedArgs { get; }
        public bool[] SupportedArgs { get; }
        public bool HasImplicitThis { get; }
        public Type? DeclaringType { get; }
        public ParameterInfo[] Parameters { get; }
        public string PrettyPrintStringFormat { get; }
    }

    public class InstrumentedMethodCache
    {
        private readonly ConcurrentDictionary<ulong, InstrumentedMethod> _cache = new();

        public InstrumentedMethodCache()
        {

        }

        public bool TryGetValue(ulong id, out InstrumentedMethod entry)
        {
            return _cache.TryGetValue(id, out entry);
        }

        public void Add(MethodInfo method, bool[] supportedArgs)
        {
            string? formattableString = PrettyPrinter.ConstructFormattableStringFromMethod(method, supportedArgs);
            if (formattableString == null)
            {
                return;
            }

            ulong id = (ulong)method.MethodHandle.Value.ToInt64();
            _cache[id] = new InstrumentedMethod(
                method,
                formattableString,
                supportedArgs,
                method.CallingConvention.HasFlag(CallingConventions.HasThis),
                method.DeclaringType,
                method.GetParameters());
        }

        public void Clear()
        {
            _cache.Clear();
        }
    }
}
