// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Reflection;
using System.Reflection.Metadata;
using System.Runtime.InteropServices;
using System.Threading;
using System.Reflection.Metadata.Ecma335;
using Microsoft.Diagnostics.Monitoring.HostingStartup;
using System.Runtime.ExceptionServices;

namespace Microsoft.Diagnostics.Monitoring.StartupHook.Snapshotter
{
    internal sealed class SnapshotterFeature
    {
        private const uint SpecialCaseBoxingTypeFlag = 0xff000000;
        private enum SpecialCaseBoxingTypes
        {
            Unknown = 0,
            Object,
            Boolean,
            Char,
            SByte,
            Byte,
            Int16,
            UInt16,
            Int32,
            UInt32,
            Int64,
            UInt64,
            Single,
            Double,
        };

        private readonly Probes.EnterProbeDelegate PinnedEnterProbe = Probes.EnterProbe;
        private static bool didInjectProbes;
        public void DoInit()
        {
            [DllImport("MonitorProfiler", CallingConvention = CallingConvention.StdCall, PreserveSig = true)]
            static extern int RegisterFunctionProbe(long enterProbeID);

            _ = RegisterFunctionProbe(PinnedEnterProbe.Method.MethodHandle.Value.ToInt64());
            Probes.InitBackgroundService();

            AppDomain.CurrentDomain.FirstChanceException += CurrentDomain_FirstChanceException;
        }

        private void CurrentDomain_FirstChanceException(object? sender, FirstChanceExceptionEventArgs e)
        {
            if (!didInjectProbes)
            {
                didInjectProbes = true;
                ThreadPool.QueueUserWorkItem(RequestProbeInjection);
            }
        }


        private static void RequestProbeInjection(object? state)
        {
            [DllImport("MonitorProfiler", CallingConvention = CallingConvention.StdCall, PreserveSig = true)]
            static extern int RequestFunctionProbeInstallation(
                [MarshalAs(UnmanagedType.LPArray)] long[] funcIds,
                long count,
                [MarshalAs(UnmanagedType.LPArray)] int[] boxingTokens,
                [MarshalAs(UnmanagedType.LPArray)] long[] boxingTokenCounts);

            Thread.Sleep(TimeSpan.FromSeconds(3));

            if (!InProcLoggerService.IsAvailable)
            {
                Console.WriteLine("In-proc logger service is not available");
            }

            MethodInfo? resolvedMethod = ResolveMethod("Mvc.dll", "Benchmarks.Controllers.MyController`1", "JsonNk");
            if (resolvedMethod == null)
            {
                return;
            }

            List<MethodInfo> methodsToInsertProbes = new()
            {
                resolvedMethod
            };

            List<long> functionIds = new(methodsToInsertProbes.Count);
            List<long> tokenCounts = new(methodsToInsertProbes.Count);
            List<int> boxingTokens = new();

            foreach (MethodInfo method in methodsToInsertProbes)
            {
                // While this function id won't be correct for all instances of the method (e.g. generics)
                // it will still work as a cache handle / uniqueifier for retrieving a cached methodbase.
                long functionId = method.MethodHandle.Value.ToInt64();

                //  GetTypeRefTokens(method.Module.Assembly)
                int[] methodBoxingTokens = GetBoxingTokens(method, new Dictionary<string, int>());
                bool[] argsSupported = new bool[methodBoxingTokens.Length];
                for (int i = 0; i < methodBoxingTokens.Length; i++)
                {
                    argsSupported[i] = methodBoxingTokens[i] != (int)TypeCode.Empty;
                }

                Probes.RegisterMethodToProbeInCache(functionId, method, argsSupported);
                functionIds.Add(functionId);

                tokenCounts.Add(methodBoxingTokens.Length);
                boxingTokens.AddRange(methodBoxingTokens);
            }

            _ = RequestFunctionProbeInstallation(functionIds.ToArray(), functionIds.Count, boxingTokens.ToArray(), tokenCounts.ToArray());
        }

        private static MethodInfo? ResolveMethod(string dll, string className, string methodName)
        {
            // Console.WriteLine($"Requesting probes in {dll}!{className}.{methodName}");

            Module? userMod = null;
            Assembly? userAssembly = null;
            var assemblies = AppDomain.CurrentDomain.GetAssemblies();
            foreach (var assembly in assemblies)
            {
                foreach (var mod in assembly.Modules)
                {
                    if (mod.Name == dll)
                    {
                        userAssembly = assembly;
                        userMod = mod;
                        break;
                    }
                }
            }
            if (userMod == null || userAssembly == null)
            {
                Console.WriteLine("COULD NOT RESOLVE REMOTE MODULE");
                return null;
            }

            Type? remoteClass = userAssembly.GetType(className);
            if (remoteClass == null)
            {
                foreach (var c in userAssembly.GetTypes())
                {
                    Console.WriteLine(c.AssemblyQualifiedName);
                }
                Console.WriteLine("COULD NOT RESOLVE REMOTE CLASS");
                return null;
            }

            MethodInfo? methodInfo = remoteClass.GetMethod(methodName);
            if (methodInfo == null)
            {
                foreach (var c in remoteClass.GetMethods())
                {
                    Console.WriteLine(c.Name);
                }
                Console.WriteLine("COULD NOT RESOLVE REMOTE METHOD");
                return null;
            }

            return methodInfo;
        }

        private static IDictionary<string, int> GetTypeRefTokens(Assembly assembly)
        {
            Dictionary<string, int> nameToTyperefToken = new();
            if (assembly == null)
            {
                return nameToTyperefToken;
            }

            return nameToTyperefToken;
        }

        private static int GetBoxingType(TypeCode typeCode)
        {
            return (int)(SpecialCaseBoxingTypeFlag | (uint)GetSpecialCaseBoxingType(typeCode));
        }

        private static int GetBoxingType(SpecialCaseBoxingTypes type)
        {
            return (int)(SpecialCaseBoxingTypeFlag | (uint)type);
        }

        private static SpecialCaseBoxingTypes GetSpecialCaseBoxingType(TypeCode typeCode)
        {
            switch (typeCode)
            {
                case TypeCode.Object:
                    return SpecialCaseBoxingTypes.Object;
                case TypeCode.Boolean:
                    return SpecialCaseBoxingTypes.Boolean;
                case TypeCode.Char:
                    return SpecialCaseBoxingTypes.Char;
                case TypeCode.SByte:
                    return SpecialCaseBoxingTypes.SByte;
                case TypeCode.Byte:
                    return SpecialCaseBoxingTypes.Byte;
                case TypeCode.Int16:
                    return SpecialCaseBoxingTypes.Int16;
                case TypeCode.UInt16:
                    return SpecialCaseBoxingTypes.UInt16;
                case TypeCode.Int32:
                    return SpecialCaseBoxingTypes.Int32;
                case TypeCode.UInt32:
                    return SpecialCaseBoxingTypes.UInt32;
                case TypeCode.Int64:
                    return SpecialCaseBoxingTypes.Int64;
                case TypeCode.UInt64:
                    return SpecialCaseBoxingTypes.UInt64;
                case TypeCode.Single:
                    return SpecialCaseBoxingTypes.Single;
                case TypeCode.Double:
                    return SpecialCaseBoxingTypes.Double;
                default:
                    return SpecialCaseBoxingTypes.Unknown;
            }
        }

        private static int[] GetBoxingTokens(MethodInfo method, IDictionary<string, int> typerefTokens)
        {
            ParameterInfo[] methodParams = method.GetParameters();
            List<Type> methodParamTypes = methodParams.Select(p => p.ParameterType).ToList();

            List<int> boxingTokens = new List<int>(methodParams.Length);

            int unsupported = GetBoxingType(SpecialCaseBoxingTypes.Unknown);
            int skipBoxing = GetBoxingType(SpecialCaseBoxingTypes.Object);

            if (method.CallingConvention.HasFlag(CallingConventions.HasThis))
            {
                Debug.Assert(!method.IsStatic);

                Type? contextfulType = method.DeclaringType;
                if (contextfulType == null)
                {
                    boxingTokens.Add(unsupported);
                }
                else
                {
                    methodParamTypes.Insert(0, contextfulType);
                }
            }

            /*
            unsafe
            {
                int rawMetadataLength = 0;
                byte* rawMetadata = null;

                if (method.Module.Assembly?.TryGetRawMetadata(out rawMetadata, out rawMetadataLength) != true)
                {
                    // something interesting
                }

                MetadataReader reader = new(rawMetadata, rawMetadataLength);
                MethodDefinition methodDef = reader.GetMethodDefinition((MethodDefinitionHandle)method.MethodHandle);
                ParameterHandleCollection parameters = methodDef.GetParameters();
                foreach (ParameterHandle paramHandle in parameters)
                {
                    Parameter param = reader.GetParameter(paramHandle);
                    if (param.SequenceNumber == 0)
                    {
                        // Return value
                        continue;
                    }

                    //reader.GetString();
                }
            }
            */

            foreach (Type paramType in methodParamTypes)
            {
                if (paramType == null)
                {
                    boxingTokens.Add(unsupported);
                }
                else if (paramType.IsByRef ||
                    paramType.IsByRefLike ||
                    paramType.IsPointer)
                {
                    boxingTokens.Add(unsupported);
                }
                else if (paramType.IsPrimitive)
                {
                    boxingTokens.Add(GetBoxingType(Type.GetTypeCode(paramType)));
                }
                else if (paramType.IsValueType)
                {
                    // Ref structs have already been filtered out by the above IsByRefLike check.

                    if (paramType.IsGenericType)
                    {
                        // Typespec
                        boxingTokens.Add(unsupported);
                    }
                    else if (paramType.Assembly != method.Module.Assembly)
                    {
                        // Typeref
                        if (paramType.FullName != null && typerefTokens.TryGetValue(paramType.FullName, out int token))
                        {
                            boxingTokens.Add(token);
                        }
                        else
                        {
                            boxingTokens.Add(unsupported);
                        }
                    }
                    else
                    {
                        // Typedef
                        boxingTokens.Add(paramType.MetadataToken);
                    }
                }
                else if (paramType.HasMetadataToken())
                {
                    boxingTokens.Add(skipBoxing);
                }
                else
                {
                    // string?
                    boxingTokens.Add(GetBoxingType(Type.GetTypeCode(paramType)));
                }
            }

            return boxingTokens.ToArray();
        }
    }
}
