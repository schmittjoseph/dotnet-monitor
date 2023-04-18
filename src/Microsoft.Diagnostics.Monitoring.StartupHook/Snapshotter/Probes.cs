// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using System.Diagnostics;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Text;
using System;
using static Microsoft.Diagnostics.Monitoring.StartupHook.InProcLogger;

namespace Microsoft.Diagnostics.Monitoring.StartupHook.Snapshotter
{
    internal sealed class Probes
    {
        [MethodImpl(MethodImplOptions.NoInlining)]
        internal static void LeaveProbe(uint funcId)
        {
            MethodBase? methodBase = new StackFrame(1, needFileInfo: false)?.GetMethod();
            if (methodBase == null)
            {
                return;
            }

            InProcLogger.Log($"[leave] {methodBase.Module}!{methodBase.DeclaringType?.FullName}.{methodBase.Name}", LogLevel.Warning);

            return;
        }

        [MethodImpl(MethodImplOptions.NoInlining)]
        internal static void EnterProbe(uint funcId, bool hasThis, object[] args)
        {
            MethodBase? methodBase = new StackFrame(1, needFileInfo: false).GetMethod();
            if (methodBase == null)
            {
                return;
            }

            StringBuilder stringBuilder = new StringBuilder();
            StringBuilder argValueBuilder = new StringBuilder();

            // JSFIX: Performance
            // 1. call back into the profiler to resolve the function ids to strings.
            // 2. 


            // Convert the handle from the profiler into a methodbase.

            // JSFIX: Either cache this and ask later on
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
                    catch (Exception)
                    {
                        stringBuilder.Append("{internal error}");
                    }
                }
                catch (Exception e)
                {
                    Console.WriteLine("===== HOOK ERROR =====");
                    Console.WriteLine($"func: {funcId}");
                    Console.WriteLine($"args: {args.Length}");
                    foreach (object arg in args)
                    {
                        Console.WriteLine(arg.GetType());
                    }
                    Console.WriteLine(e);
                }
            }
            stringBuilder.Append(')');

            InProcLogger.Log(stringBuilder.ToString());

            return;
        }

        private static void SerializeObject(StringBuilder stringBuilder, object value)
        {
            if (value == null)
            {
                stringBuilder.Append("null");
                return;
            }
            else if (value.GetType().IsArray)
            {
                // JSFIX: Upper-bound for perf reasons
                // [a, b, c, d, ...]12345 <- how to indicate length on truncate/always?
                // JSFIX: Enumerables...
                int j = 0;
                Array? arrayValue = value as Array;
                if (arrayValue == null)
                {
                    stringBuilder.Append("{internal error}");
                    return;
                }

                stringBuilder.Append($"(length: {arrayValue.Length})");
                stringBuilder.Append('[');
                foreach (object element in arrayValue)
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
            }
            else
            {
                // Convert.ToString will try using IConvertible and IFormattable before calling .ToString
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
        }

        private static void WrapValue(StringBuilder stringBuilder, string? value)
        {
            stringBuilder.Append('\'');
            stringBuilder.Append(value ?? string.Empty);
            stringBuilder.Append('\'');
        }
    }
}
