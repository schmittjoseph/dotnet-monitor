// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using System;
using System.Diagnostics;
using System.Diagnostics.Tracing;
using System.Threading;

namespace Microsoft.Diagnostics.Monitoring.UnitTestApp.Scenarios
{
    internal sealed class StacksWorker : IDisposable
    {
        [StackTraceHidden]
        private static void WaitForHandleWithHiddenMethodFrames(WaitHandle waitHandle)
        {
            PartiallyVisibleClass partiallyVisibleClass = new();
            partiallyVisibleClass.WaitForHandle(waitHandle);
        }

        [StackTraceHidden]
        private abstract class BaseHiddenClass
        {
#pragma warning disable CA1822 // Mark members as static
            public void WaitForHandleFromBaseClass(WaitHandle waitHandle)
#pragma warning restore CA1822 // Mark members as static
            {
                using EventSource eventSource = new EventSource("StackScenario");
                using EventCounter eventCounter = new EventCounter("Ready", eventSource);
                eventCounter.WriteMetric(1.0);
                waitHandle.WaitOne();
            }
        }

        private class PartiallyVisibleClass : BaseHiddenClass
        {
            // StackTraceHidden attributes are not inherited
            public void WaitForHandle(WaitHandle waitHandle)
            {
                WaitForHandleFromBaseClass(waitHandle);
            }
        }

        private EventWaitHandle _eventWaitHandle = new ManualResetEvent(false);

        public sealed class StacksWorkerNested<T>
        {
            private WaitHandle _handle;

            public void DoWork<U>(U test, WaitHandle handle)
            {
                _handle = handle;
                MonitorLibrary.TestHook(Callback);
            }

            public void Callback()
            {
                WaitForHandleWithHiddenMethodFrames(_handle);
            }
        }

        public void Work()
        {
            StacksWorkerNested<int> nested = new StacksWorkerNested<int>();

            nested.DoWork<long>(5, _eventWaitHandle);
        }

        public void Signal()
        {
            _eventWaitHandle.Set();
        }

        public void Dispose()
        {
            _eventWaitHandle.Dispose();

        }
    }
}
