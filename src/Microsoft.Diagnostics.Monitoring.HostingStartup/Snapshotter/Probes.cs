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

namespace Microsoft.Diagnostics.Monitoring.StartupHook.Snapshotter
{
    public static class Probes
    {
        private static readonly ConcurrentDictionary<long, (MethodBase, bool[])> methodBaseLookup = new();
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

        public static void RegisterMethodToProbeInCache(long uniquifier, MethodBase method, bool[] argsSupported)
        {
            lock (Locker)
            {
                methodBaseLookup[uniquifier] = (method, argsSupported);
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
                    methodBaseLookup.Clear();
                }
                catch (Exception ex)
                {
                    InProcLoggerService.Log($"Internal error: Failed to uninstall probes: {ex}", LogLevel.Critical);
                }

                requestProbeStopEvent.Reset();
            }
        }

        private static (MethodBase, bool[])? GetMethodInformation(long uniquifier)
        {
            if (methodBaseLookup.TryGetValue(uniquifier, out (MethodBase, bool[]) cachedValue))
            {
                return cachedValue;
            }

            return null;
        }


        public static void EnterProbeSlim(long uniquifier)
        {
            Console.WriteLine("ENTER");
        }

        public static void EnterProbe(long uniquifier, object[] args)
        {
            (MethodBase, bool[])? methodInformation = GetMethodInformation(uniquifier);
            if (!methodInformation.HasValue)
            {
                return;
            }

            MethodBase method = methodInformation.Value.Item1;
            bool[] argsSupported = methodInformation.Value.Item2;

            bool hasThis = !method.IsStatic;

            StringBuilder stringBuilder = new StringBuilder();
            StringBuilder argValueBuilder = new StringBuilder();

            stringBuilder.Append($"[enter] {method.Module}");


            // JSFIX: Cache this
            string className = method.DeclaringType?.FullName?.Split('`')?[0] ?? string.Empty;
            stringBuilder.Append(className);
            PrettyPrintGenericArgs(stringBuilder, method.DeclaringType?.GetGenericArguments());

            stringBuilder.Append($".{method.Name}");
            PrettyPrintGenericArgs(stringBuilder, method.GetGenericArguments());

            stringBuilder.Append('(');
            var parameters = method.GetParameters(); // JSFIX: Cache
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

                    if (!argsSupported[i])
                    {
                        stringBuilder.Append("{unsupported}");
                        continue;
                    }

                    argValueBuilder.Clear();
                    try
                    {
                        Type? type = null;
                        int paramI = (hasThis) ? i - 1 : i;
                        if (paramI >= 0)
                        {
                            type = parameters[paramI].ParameterType;
                        }
                        else
                        {
                            type = method.DeclaringType;
                        }

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
