// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Threading.Tasks;

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





    internal enum MyEnum
    {
        Val1,
        Val2,
        Val3
    };

    internal sealed class SnapshotterFeature
    {
        public delegate void EnterProbePointer(uint funcId, bool hasThis, object[] args);
        public delegate void LeaveProbePointer(uint a);
        public delegate void TestFunction(
            uint i,
            bool? test,
            string hi,
            int[,] t,
            List<bool> f,
            FooBar foo,
            MyEnum myEnum,
            ref int refInt,
            out int outInt,
            (IList<IList<SnapshotterFeature>>, FooBar) tuple
            );

        private readonly EnterProbePointer PinnedEnterProbe;
        private readonly TestFunction PinnedTestFunc;

        private static SnapshotterFeature? me;


        public SnapshotterFeature()
        {
            me = this;

            PinnedEnterProbe = new EnterProbePointer(Probes.EnterProbe);
            PinnedTestFunc = new TestFunction(Test);
        }

        private static async Task DoWork()
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

                    me?.Test((uint)Random.Shared.Next(), testVal, "Hello world!", t, f, foo, MyEnum.Val1, ref i, out int j, (new List<IList<SnapshotterFeature>>(), foo));
                }
                catch (Exception ex)
                {
                    Console.WriteLine(ex.ToString());
                }
                await Task.Delay(1000);
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
            ref int refInt,
            out int outInt,
            (IList<IList<SnapshotterFeature>>, FooBar) tuple
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

            [DllImport("MonitorProfiler", CallingConvention = CallingConvention.StdCall, PreserveSig = true)]
            static extern int RequestFunctionProbeInstallation([MarshalAs(UnmanagedType.LPArray)] long[] array, long count);

            long enterFunctionId = PinnedEnterProbe.Method.MethodHandle.Value.ToInt64();

            _ = RegisterFunctionProbe(enterFunctionId);
            Probes.InitBackgroundService();


            long[] funcIds = new[] { PinnedTestFunc.Method.MethodHandle.Value.ToInt64() };
            _ = RequestFunctionProbeInstallation(funcIds, funcIds.Length);


            Task.Run(DoWork);
        }
    }
}
