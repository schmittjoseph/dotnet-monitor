// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using System.Diagnostics;
using System.Reflection;
using System.Runtime.CompilerServices;
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
        private static readonly ConcurrentDictionary<(uint, uint), MethodBase?> methodBaseLookup = new();
#pragma warning disable CS0649 // Field 'Probes.LogTypes' is never assigned to, and will always have its default value false
        private static bool LogTypes;
#pragma warning restore CS0649 // Field 'Probes.LogTypes' is never assigned to, and will always have its default value false

        [DllImport("MonitorProfiler", CallingConvention = CallingConvention.StdCall, PreserveSig = true)]
        static extern int RequestFunctionProbeShutdown();


        private static ManualResetEventSlim requestProbeStopEvent = new(initialState: false);

        private static volatile int CountDown = 2;

        public static void InitBackgroundService()
        {
            ThreadPool.QueueUserWorkItem(ProbeHandler);
        }

        private static void ProbeHandler(object? state)
        {
            while (true)
            {
                requestProbeStopEvent.Wait();

                try
                {
                    RequestFunctionProbeShutdown();
                    methodBaseLookup.Clear();
                }
                catch (Exception ex)
                {
                    InProcLoggerService.Log($"Internal error: Failed to uninstall probes: {ex}", LogLevel.Critical);
                }

                requestProbeStopEvent.Reset();
            }
        }


        [MethodImpl(MethodImplOptions.NoInlining)]
        private static MethodBase? GetMethodBase(uint moduleId, uint methodDef)
        {
            if (methodBaseLookup.TryGetValue((moduleId, methodDef), out MethodBase? methodBase))
            {
                return methodBase;
            }

            // Skip the ourself and the invoked probe to find what MethodBase corresponds with this function id.
            methodBase = new StackFrame(2, needFileInfo: false)?.GetMethod();
            if (methodBase != null)
            {
                if ((uint)methodBase.MetadataToken != methodDef)
                {
                    InProcLoggerService.Log($"Internal error: Could not resolve method (expected({methodDef}), actual({(uint)methodBase.MetadataToken})", LogLevel.Warning);
                    // Could not resolve.
                    return null;
                }
            }

            _ = methodBaseLookup.TryAdd((moduleId, methodDef), methodBase);

            return methodBase;
        }


        public static void EnterProbeSlim ()
        {
            Console.WriteLine("ENTER");
        }

        [MethodImpl(MethodImplOptions.NoInlining)]
        public static void EnterProbe(uint moduleId, uint methodDef, bool hasThis, object[] args)
        {
            MethodBase? methodBase = GetMethodBase(moduleId, methodDef);
            if (methodBase == null)
            {
                return;
            }

            StringBuilder stringBuilder = new StringBuilder();
            StringBuilder argValueBuilder = new StringBuilder();

            stringBuilder.Append($"[enter] {methodBase.Module}!{methodBase.DeclaringType?.FullName}.{methodBase.Name}");
            stringBuilder.Append('(');
            var parameters = methodBase.GetParameters();
            for (int i = 0; i < args?.Length; i++)
            {
                try
                {
                    if (i != 0)
                    {
                        stringBuilder.Append(", ");
                    }

                    if (hasThis && i == 0)
                    {
                        if (LogTypes)
                        {
                            stringBuilder.Append($"({args[i].GetType()}) ");
                        }
                        stringBuilder.Append("this");
                    }
                    else
                    {
                        int paramI = (hasThis) ? i - 1 : i;
                        var paramInfo = parameters[paramI];
                        if (LogTypes)
                        {
                            stringBuilder.Append($"({paramInfo.ParameterType}) ");
                        }

                        /*
                        if (paramInfo.Attributes.HasFlag(ParameterAttributes.Out))
                        {
                            stringBuilder.Append($"out ");

                            // We're an enter probe, so the out value may be uninitialized.
                        }
                        */

                        if (paramInfo.IsOut)
                        {
                            stringBuilder.Append($"out ");
                        }
                        else if (paramInfo.ParameterType.IsByRefLike)
                        {
                            stringBuilder.Append($"ref struct ");
                        }
                        else if (paramInfo.ParameterType.IsByRef)
                        {
                            stringBuilder.Append($"ref ");
                        }

                        stringBuilder.Append(paramInfo.Name);
                    }
                    stringBuilder.Append(": ");

                    argValueBuilder.Clear();
                    try
                    {
                        Type? type = null;
                        int paramI = (hasThis) ? i - 1 : i;
                        if (paramI >= 0)
                        {
                            type = parameters[paramI].ParameterType;
                        }
                        SerializeObject(argValueBuilder, args[i], type);
                        stringBuilder.Append(argValueBuilder);
                    }
                    catch (Exception ex)
                    {
                        stringBuilder.Append($"internal error: {ex}");
                    }
                }
                catch (Exception ex)
                {
                    InProcLoggerService.Log($"Internal hook error: {ex}", LogLevel.Critical);
                    requestProbeStopEvent.Set();
                }
            }
            stringBuilder.Append(')');

            InProcLoggerService.Log(stringBuilder.ToString());

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

        private static void SerializeObject(StringBuilder stringBuilder, object value, Type? typeOverride = null)
        {
            if (typeOverride?.IsByRefLike == true || // ref struct
               typeOverride?.IsByRef == true)
            {
                stringBuilder.Append("{unsupported}");
                return;
            }

            if (value == null)
            {
                stringBuilder.Append("null");
                return;
            }

            typeOverride ??= value.GetType();

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

        private static void WrapValue(StringBuilder stringBuilder, string? value)
        {
            stringBuilder.Append('\'');
            stringBuilder.Append(value ?? string.Empty);
            stringBuilder.Append('\'');
        }
    }
}
