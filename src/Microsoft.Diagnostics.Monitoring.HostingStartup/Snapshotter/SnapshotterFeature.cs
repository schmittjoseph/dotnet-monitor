// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Threading.Tasks;

namespace Microsoft.Diagnostics.Monitoring.StartupHook.Snapshotter
{
    internal sealed class SnapshotterFeature
    {
        public delegate void EnterProbePointer(uint funcId, bool hasThis, object[] args);
        public delegate void LeaveProbePointer(uint a);
        public delegate int TestFunction(uint i, bool test, string hi, int[,] t, List<bool> f);

        private readonly EnterProbePointer PinnedEnterProbe;
        private readonly LeaveProbePointer PinnedLeaveProbe;
        private readonly TestFunction PinnedTestFunc;

        private static SnapshotterFeature? me;


        public SnapshotterFeature()
        {
            me = this;

            PinnedEnterProbe = new EnterProbePointer(Probes.EnterProbe);
            PinnedLeaveProbe = new LeaveProbePointer(Probes.LeaveProbe);
            PinnedTestFunc = new TestFunction(Test);

            // Setting up the delegates
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

            while (true)
            {
                me?.Test((uint)Random.Shared.Next(), true, "Hello world!", t, f);
                await Task.Delay(1000);
            }
        }

        private int Test(uint i, bool test, string hi, int[,] t, List<bool> f)
        {
            return 0;
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
            static extern int RegisterFunctionProbes(long enterProbeID, long leaveProbeID);

            long enterFunctionId = PinnedEnterProbe.Method.MethodHandle.Value.ToInt64();
            //long leaveFunctionId = PinnedLeaveProbe.Method.MethodHandle.Value.ToInt64();
            long leaveFunctionId = PinnedTestFunc.Method.MethodHandle.Value.ToInt64();

            _ = RegisterFunctionProbes(enterFunctionId, leaveFunctionId);
            Probes.InitBackgroundService();

            Task.Run(DoWork);
        }
    }
}
