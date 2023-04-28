// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Threading;
using static Microsoft.Diagnostics.Monitoring.StartupHook.Snapshotter.SnapshotterFeature;

namespace Microsoft.Diagnostics.Monitoring.StartupHook.Snapshotter
{
    internal struct FooBar
    {
        public int Baz;

        public override string ToString()
        {
            return $"My custom struct: Baz={Baz}";
        }
    }

    internal ref struct RefFooBar
    {
        public int Baz;

        public override string ToString()
        {
            return $"My custom struct: Baz={Baz}";
        }
    }

    internal enum MyEnum
    {
        Val1,
        Val2,
        Val3
    };

    internal sealed class SnapshotterFeature
    {
        public delegate void EnterProbePointer(uint moduleId, uint methodDef, bool hasThis, object[] args);
        public delegate void LeaveProbePointer(uint a);
        public delegate void TestFunction(
            uint i,
            bool? test,
            string hi,
            int[,] t,
            List<bool> f,
            FooBar foo,
            MyEnum myEnum,
            (IList<IList<SnapshotterFeature>>, FooBar) tuple,
            ref int refInt,
            out int outInt,
            RefFooBar refFooBar
            );

        private readonly EnterProbePointer PinnedEnterProbe = Probes.EnterProbe;
        private readonly TestFunction PinnedTestFunc;

        private static SnapshotterFeature? me;


        public SnapshotterFeature()
        {
            me = this;

            PinnedTestFunc = new TestFunction(Test);
        }

        [System.Diagnostics.CodeAnalysis.SuppressMessage("Style", "IDE0049:Simplify Names", Justification = "<Pending>")]
        private static int[] GetBoxingTokens(MethodBase methodBase)
        {
            ParameterInfo[] methodParams = methodBase.GetParameters();
            List<Type> methodParamTypes = methodParams.Select(p => p.ParameterType).ToList();

            List<int> boxingTokens = new List<int>(methodParams.Length);

            const int unsupported = -1;
            const int skipBoxing = (int)TypeCode.Empty;

            if (methodBase.CallingConvention.HasFlag(CallingConventions.HasThis))
            {
                Debug.Assert(!methodBase.IsStatic);

                // Get the this type.
                Type? contextfulType = methodBase.DeclaringType;
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
                    if (paramType.IsGenericType)
                    {
                        boxingTokens.Add(unsupported);
                    }
                    else
                    {
                        // Ref structs have already been filtered out by the above IsByRefLike check.
                        boxingTokens.Add(paramType.MetadataToken);
                    }

                }
                else if (paramType.HasMetadataToken())
                {
                    boxingTokens.Add(skipBoxing);
                }
                else
                {
                    boxingTokens.Add(unsupported);
                }

            }

            return boxingTokens.ToArray();
        }


        private static void DoWork2(object? state)
        {
            [DllImport("MonitorProfiler", CallingConvention = CallingConvention.StdCall, PreserveSig = true)]
            static extern int RequestFunctionProbeInstallation([MarshalAs(UnmanagedType.LPArray)] long[] array, long count, [MarshalAs(UnmanagedType.LPArray)] int[] boxingTokens, [MarshalAs(UnmanagedType.LPArray)] long[] boxingTokenCounts);

            Console.WriteLine("Waiting 10 seconds before injecting conditional probes");
            Thread.Sleep(TimeSpan.FromSeconds(10));


            const string dll = "Mvc.dll";
            const string className = "Benchmarks.Controllers.JsonController";
            const string methodName = "JsonNk";

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
                return;
            }

            Type? remoteClass = userAssembly.GetType(className);
            if (remoteClass == null)
            {
                Console.WriteLine("COULD NOT RESOLVE REMOTE CLASS");
                return;
            }

            MethodInfo? info = remoteClass.GetMethod(methodName);
            if (info == null)
            {
                Console.WriteLine("COULD NOT RESOLVE REMOTE METHOD");
                return;
            }

            MethodBase func = info;
            long[] funcIds = new[] { func.MethodHandle.Value.ToInt64() };
            // For now just do one.

            int[] boxingTokens = GetBoxingTokens(func);
            long[] counts = new[] { (long)boxingTokens.Length };

            _ = RequestFunctionProbeInstallation(funcIds, funcIds.Length, boxingTokens, counts);

        }

        private static void DoWork(object? state)
        {
            int[,] t = new int[2, 2]
            {
                {0, 1 },
                {5, 6 },
            };

            List<bool> f = new()
            {
                false,
                true
            };

            FooBar foo = new FooBar
            {
                Baz = 10
            };

            int i = 0;
            while (true)
            {
                try
                {
                    //TestHooks.Test((uint)Random.Shared.Next(), true, "Hello world!", t, f);
                    bool? testVal = null;
                    i++;
                    if (i == 1)
                    {
                        testVal = true;
                    }
                    else if (i == 2)
                    {
                        testVal = false;
                    }
                    else if (i >= 3)
                    {
                        i = 0;
                    }

                    RefFooBar refFooBar = new RefFooBar
                    {
                        Baz = 20
                    };

                    me?.Test(
                        (uint)Random.Shared.Next(),
                        testVal,
                        "Hello world!",
                        t,
                        f,
                        foo,
                        MyEnum.Val1,
                        (new List<IList<SnapshotterFeature>>(), foo),
                        ref i,
                        out int j,
                        refFooBar);
                }
                catch (Exception ex)
                {
                    Console.WriteLine(ex.ToString());
                }

                Thread.Sleep(1000);
            }
        }

        private void Test(
            uint i,
            bool? test,
            string hi,
            int[,] t,
            List<bool> f,
            FooBar foo,
            MyEnum myEnum,
            (IList<IList<SnapshotterFeature>>, FooBar) tuple,
            ref int refInt,
            out int outInt,
            RefFooBar refFooBar
            )
        {
            outInt = 0;
            return;
        }

        /*
        public static IntPtr ResolveDllImport(string libraryName, Assembly assembly, DllImportSearchPath? searchPath)
        {
            //DllImport for Windows automatically loads in-memory modules (such as the profiler). This is not the case for Linux/MacOS.
            // If we fail resolving the DllImport, we have to load the profiler ourselves.
            string profilerName = ProfilerHelper.GetPath(RuntimeInformation.ProcessArchitecture);
            if (NativeLibrary.TryLoad(profilerName, out IntPtr handle))
            {
                return handle;
            }

            return IntPtr.Zero;
        }
    */

        public override string ToString()
        {
            return "My custom object";
        }

        public void DoInit()
        {
            [DllImport("MonitorProfiler", CallingConvention = CallingConvention.StdCall, PreserveSig = true)]
            static extern int RegisterFunctionProbe(long enterProbeID);

            
            // [DllImport("MonitorProfiler", CallingConvention = CallingConvention.StdCall, PreserveSig = true)]
            // static extern int RequestFunctionProbeInstallation([MarshalAs(UnmanagedType.LPArray)] long[] array, long count, [MarshalAs(UnmanagedType.LPArray)] int[] boxingTokens, [MarshalAs(UnmanagedType.LPArray)] long[] boxingTokenCounts);
            
            long enterFunctionId = PinnedEnterProbe.Method.MethodHandle.Value.ToInt64();

            _ = RegisterFunctionProbe(enterFunctionId);
            Probes.InitBackgroundService();


            MethodBase func = PinnedTestFunc.Method;
            long[] funcIds = new[] { func.MethodHandle.Value.ToInt64() };
            // For now just do one.

            int[] boxingTokens = GetBoxingTokens(func);
            long[] counts = new[] { (long)boxingTokens.Length };

            //_ = RequestFunctionProbeInstallation(funcIds, funcIds.Length, boxingTokens, counts);


            ThreadPool.QueueUserWorkItem(DoWork2);
        }
    }
}
