// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using System.Reflection;
using System.Text;
using System;
using System.Collections.Concurrent;
using Microsoft.Extensions.Logging;
using Microsoft.Diagnostics.Monitoring.HostingStartup;
using System.Runtime.InteropServices;
using System.Threading;
using System.Collections;
using System.Linq;

namespace Microsoft.Diagnostics.Monitoring.StartupHook.Snapshotter
{
    public static class Probes
    {
        private readonly struct MethodCacheEntry
        {
            public MethodCacheEntry(
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
                HasImplicitThis = hasImplicitThis;
                DeclaringType = declaringType;
                Parameters = parameters;
            }

            public MethodInfo MethodInfo { get; }

            public bool[] SupportedArgs { get; }

            public bool HasImplicitThis { get; }

            public Type? DeclaringType { get; }

            public ParameterInfo[] Parameters { get; }

            public string PrettyPrintStringFormat { get; }
        }

        public delegate void EnterProbeDelegate(long uniquifier, object[] args);

        private static readonly ConcurrentDictionary<long, MethodCacheEntry> methodLookup = new();
#pragma warning disable CS0649 // Field 'Probes.LogTypes' is never assigned to, and will always have its default value false
        private static bool LogTypes;
#pragma warning restore CS0649 // Field 'Probes.LogTypes' is never assigned to, and will always have its default value false

        [DllImport("MonitorProfiler", CallingConvention = CallingConvention.StdCall, PreserveSig = true)]
        static extern int RequestFunctionProbeShutdown();

        private static readonly object Locker = new();

        private static ManualResetEventSlim requestProbeStopEvent = new(initialState: false);

        private static volatile int CountDown = 2;

        public static void InitBackgroundService()
        {
            ThreadPool.QueueUserWorkItem(ProbeHandler);
        }

        private static void EmitEscapedString(StringBuilder stringBuilder, string value)
        {
            stringBuilder.Append("{{");
            stringBuilder.Append(value);
            stringBuilder.Append("}}");
        }

        private static bool EmitParameter(StringBuilder stringBuilder, Type? type, string? name, bool isSupported, int formatIndex, ParameterInfo? paramInfo = null)
        {
            if (LogTypes)
            {
                stringBuilder.Append('(');
                if (type == null)
                {
                    EmitEscapedString(stringBuilder, "unknown");
                }
                else
                {
                    stringBuilder.Append(type);
                }

                stringBuilder.Append(") ");
            }

            if (paramInfo?.IsOut == true)
            {
                stringBuilder.Append("out ");
            }
            else if (type?.IsByRefLike == true)
            {
                stringBuilder.Append("ref struct ");
            }
            else if (type?.IsByRef == true)
            {
                stringBuilder.Append("ref ");
            }

            if (name == null)
            {
                EmitEscapedString(stringBuilder, "unknown");
            }
            else
            {
                stringBuilder.Append(name);
            }

            stringBuilder.Append(": ");

            if (isSupported)
            {
                stringBuilder.Append('{');
                stringBuilder.Append(formatIndex);
                stringBuilder.Append('}');
                return true;
            }
            else
            {
                EmitEscapedString(stringBuilder, "unsupported");
                return false;
            }
        }

        public static void RegisterMethodToProbeInCache(long uniquifier, MethodInfo method, bool[] supportedArgs)
        {
            StringBuilder fmtStringBuilder = new();

            fmtStringBuilder.Append($"[enter] {method.Module}!");
            string className = method.DeclaringType?.FullName?.Split('`')?[0] ?? string.Empty;
            fmtStringBuilder.Append(className);
            PrettyPrintGenericArgs(fmtStringBuilder, method.DeclaringType?.GetGenericArguments());

            fmtStringBuilder.Append($".{method.Name}");
            PrettyPrintGenericArgs(fmtStringBuilder, method.GetGenericArguments());

            fmtStringBuilder.Append('(');

            int fmtIndex = 0;
            int index = 0;
            ParameterInfo[] parameters = method.GetParameters();
            if (method.CallingConvention.HasFlag(CallingConventions.HasThis))
            {
                if(EmitParameter(fmtStringBuilder, method.DeclaringType, "this", supportedArgs[index], fmtIndex))
                {
                    fmtIndex++;
                }
                index++;
            }

            foreach (ParameterInfo paramInfo in parameters)
            {
                if (fmtIndex != 0)
                {
                    fmtStringBuilder.Append(", ");
                }

                if (EmitParameter(fmtStringBuilder, paramInfo.ParameterType, paramInfo.Name, supportedArgs[index], fmtIndex, paramInfo))
                {
                    fmtIndex++;
                }

                index++;
            }

            fmtStringBuilder.Append(')');

            lock (Locker)
            {
                
                methodLookup[uniquifier] = new MethodCacheEntry(
                    method,
                    fmtStringBuilder.ToString(),
                    supportedArgs,
                    method.CallingConvention.HasFlag(CallingConventions.HasThis),
                    method.DeclaringType,
                    parameters);
            }
        }

        private static void ProbeHandler(object? state)
        {
            while (true)
            {
                requestProbeStopEvent.Wait();

                try
                {
                    RequestFunctionProbeShutdown();
                    methodLookup.Clear();
                }
                catch (Exception ex)
                {
                    InProcLoggerService.Log($"Internal error: Failed to uninstall probes: {ex}", LogLevel.Critical);
                }

                requestProbeStopEvent.Reset();
            }
        }

        private static MethodCacheEntry? GetMethodInformation(long uniquifier)
        {
            if (methodLookup.TryGetValue(uniquifier, out MethodCacheEntry cacheEntry))
            {
                return cacheEntry;
            }

            return null;
        }


        public static void EnterProbeSlim(long uniquifier, object[] args)
        {
            Console.WriteLine("ENTER");
        }

        public static void EnterProbe(long uniquifier, object[] args)
        {
            if (args == null)
            {
                return;
            }

            MethodCacheEntry? cacheEntry = GetMethodInformation(uniquifier);
            if (!cacheEntry.HasValue)
            {
                return;
            }

            StringBuilder argBuilder = new();
            string[] argValues = new string[args.Length];
            for (int i = 0; i < args?.Length; i++)
            {
                if (!cacheEntry.Value.SupportedArgs[i])
                {
                    continue;
                }

                SerializeObject(argBuilder, args[i]);
                argValues[i] = argBuilder.ToString();
                argBuilder.Clear();
            }

            InProcLoggerService.Log(string.Format(cacheEntry.Value.PrettyPrintStringFormat, argValues));

            if (CountDown > 0)
            {
                CountDown--;
                if (CountDown <= 0)
                {
                    requestProbeStopEvent.Set();
                }
            }

            return;
        }

        private static void SerializeObject(StringBuilder stringBuilder, object value)
        {
            if (value == null)
            {
                stringBuilder.Append("null");
                return;
            }

            //  else if (typeOverride.IsArray)
            // https://learn.microsoft.com/dotnet/csharp/programming-guide/arrays/multidimensional-arrays (rank)
            IEnumerable? enumerable = (value as IEnumerable);
            if (enumerable != null && value is not string)
            {
                stringBuilder.Append('[');
                int j = 0;
                foreach (object element in enumerable)
                {
                    if (j != 0)
                    {
                        stringBuilder.Append(", ");
                    }

                    if (j > 10)
                    {
                        stringBuilder.Append("{...truncated}");
                        break;
                    }

                    SerializeObject(stringBuilder, element);
                    j++;
                }
                stringBuilder.Append(']');
                return;
            }

            if (value is IConvertible ic and not string)
            {
                stringBuilder.Append(ic.ToString(null) ?? string.Empty);
            }
            else if (value is IFormattable formattable)
            {
                WrapValue(stringBuilder, formattable.ToString(null, null));
            }
            else
            {
                WrapValue(stringBuilder, value.ToString());
            }
        }

        private static void PrettyPrintGenericArgs(StringBuilder stringBuilder, Type[]? genericArgs)
        {
            if (genericArgs == null)
            {
                return;
            }

            if (genericArgs.Length > 0)
            {
                stringBuilder.Append('<');
                int i = 0;
                foreach (var g in genericArgs)
                {
                    if (i != 0)
                    {
                        stringBuilder.Append(", ");
                    }

                    stringBuilder.Append(g.Name);

                    i++;
                }
                stringBuilder.Append('>');
            }
        }

        private static void WrapValue(StringBuilder stringBuilder, string? value)
        {
            stringBuilder.Append('\'');
            stringBuilder.Append(value ?? string.Empty);
            stringBuilder.Append('\'');
        }
    }
}
