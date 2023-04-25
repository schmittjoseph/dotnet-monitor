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

namespace Microsoft.Diagnostics.Monitoring.StartupHook.Snapshotter
{
    internal static class Probes
    {
        private static readonly ConcurrentDictionary<uint, MethodBase?> funcIdToMethodBase = new();

        [DllImport("MonitorProfiler", CallingConvention = CallingConvention.StdCall, PreserveSig = true)]
        static extern int RequestFunctionProbeShutdown();


        private static ManualResetEventSlim requestProbeStopEvent = new(initialState: false);

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
                    funcIdToMethodBase.Clear();
                }
                catch (Exception ex)
                {
                    InProcLoggerService.Log($"Internal error: Failed to uninstall probes: {ex}", LogLevel.Critical);
                }

                requestProbeStopEvent.Reset();
            }
        }


        [MethodImpl(MethodImplOptions.NoInlining)]
        private static MethodBase? GetMethodBase(uint funcId)
        {
            if (funcIdToMethodBase.TryGetValue(funcId, out MethodBase? methodBase))
            {
                return methodBase;
            }

            // Skip the ourself and the invoked probe to find what MethodBase corresponds with this function id.
            methodBase = new StackFrame(2, needFileInfo: false)?.GetMethod();
            if (methodBase != null)
            {
                if ((uint)methodBase.MethodHandle.Value.ToInt64() != funcId)
                {
                    InProcLoggerService.Log("Internal error: Could not resolve method", LogLevel.Warning);
                    // Could not resolve.
                    return null;
                }
            }

            _ = funcIdToMethodBase.TryAdd(funcId, methodBase);

            return methodBase;
        }

        [MethodImpl(MethodImplOptions.NoInlining)]
        internal static void LeaveProbe(uint funcId)
        {
            MethodBase? methodBase = GetMethodBase(funcId);
            if (methodBase == null)
            {
                return;
            }

            InProcLoggerService.Log($"[leave] {methodBase.Module}!{methodBase.DeclaringType?.FullName}.{methodBase.Name}", LogLevel.Warning);

            return;
        }

        [MethodImpl(MethodImplOptions.NoInlining)]
        internal static void EnterProbe(uint funcId, bool hasThis, object[] args)
        {
            MethodBase? methodBase = GetMethodBase(funcId);
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
                        stringBuilder.Append($"({args[i].GetType()}) this");
                    }
                    else
                    {
                        int paramI = (hasThis) ? i - 1 : i;
                        stringBuilder.Append($"({parameters[paramI].ParameterType}) {parameters[paramI].Name}");
                    }
                    stringBuilder.Append(": ");

                    argValueBuilder.Clear();
                    try
                    {
                        SerializeObject(argValueBuilder, args[i]);
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

            return;
        }

        private static void SerializeObject(StringBuilder stringBuilder, object value, Type? typeOverride = null)
        {
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

            // https://learn.microsoft.com/dotnet/csharp/language-reference/builtin-types/nullable-value-types#how-to-identify-a-nullable-value-type
            Type? nullableBaseType = Nullable.GetUnderlyingType(typeOverride);
            if (nullableBaseType != null)
            {
                Console.WriteLine("YUP");
                SerializeObject(stringBuilder, value, nullableBaseType);
                return;
            }

            if (value is IConvertible ic)
            {
                stringBuilder.Append(ic.ToString(null));
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
