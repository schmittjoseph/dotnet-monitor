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

namespace Microsoft.Diagnostics.Monitoring.StartupHook.Snapshotter
{
    internal sealed class SnapshotterFeature
    {
        private readonly Probes.EnterProbeDelegate PinnedEnterProbe = Probes.EnterProbe;

        public void DoInit()
        {
            [DllImport("MonitorProfiler", CallingConvention = CallingConvention.StdCall, PreserveSig = true)]
            static extern int RegisterFunctionProbe(long enterProbeID);

            _ = RegisterFunctionProbe(PinnedEnterProbe.Method.MethodHandle.Value.ToInt64());
            Probes.InitBackgroundService();

            ThreadPool.QueueUserWorkItem(RequestProbeInjection);
        }

        private static void RequestProbeInjection(object? state)
        {
            [DllImport("MonitorProfiler", CallingConvention = CallingConvention.StdCall, PreserveSig = true)]
            static extern int RequestFunctionProbeInstallation(
                [MarshalAs(UnmanagedType.LPArray)] long[] funcIds,
                long count,
                [MarshalAs(UnmanagedType.LPArray)] int[] boxingTokens,
                [MarshalAs(UnmanagedType.LPArray)] long[] boxingTokenCounts);

            Console.WriteLine("Waiting 10 seconds before injecting probes");
            Thread.Sleep(TimeSpan.FromSeconds(10));

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

                int[] methodBoxingTokens = GetBoxingTokens(method);
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
            Console.WriteLine($"Requesting remote probes in {dll}!{className}.{methodName}");

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

        private static int[] GetBoxingTokens(MethodInfo method)
        {
            ParameterInfo[] methodParams = method.GetParameters();
            List<Type> methodParamTypes = methodParams.Select(p => p.ParameterType).ToList();

            List<int> boxingTokens = new List<int>(methodParams.Length);

            const int unsupported = (int)TypeCode.Empty;
            const int skipBoxing = (int)TypeCode.Object;

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
                    boxingTokens.Add((int)Type.GetTypeCode(paramType));
                }
                else if (paramType.IsValueType)
                {
                    // Ref structs have already been filtered out by the above IsByRefLike check.

                    if (paramType.IsGenericType)
                    {
                        // Typespec
                        boxingTokens.Add(unsupported);
                    }
                    else if(paramType.Assembly != method.Module.Assembly)
                    {
                        #region typeref_resolver
                        // Typeref
                        unsafe
                        {
                            int rawMetadataLength = 0;
                            byte* rawMetadata = null;

                            // MetadataLoadContext - https://github.com/dotnet/runtime/issues/28887

                            if (method.Module.Assembly?.TryGetRawMetadata(out rawMetadata, out rawMetadataLength) != true)
                            {
                                boxingTokens.Add(unsupported);
                            }

                            MetadataReader reader = new(rawMetadata, rawMetadataLength);
                            foreach(TypeReferenceHandle handle in reader.TypeReferences)
                            {
                                TypeReference typeRef = reader.GetTypeReference(handle);
                            }
                        }
                        #endregion

                        // Typeref
                        boxingTokens.Add(unsupported);
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
                    boxingTokens.Add((int)Type.GetTypeCode(paramType));
                }
            }

            return boxingTokens.ToArray();
        }

    }
}
