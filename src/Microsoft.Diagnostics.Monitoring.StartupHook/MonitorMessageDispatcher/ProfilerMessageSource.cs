// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.


using System.Runtime.InteropServices;
using System.IO;
using System.Reflection;
using System;
using Microsoft.Diagnostics.Tools.Monitor.Profiler;

namespace Microsoft.Diagnostics.Monitoring.StartupHook.MonitorMessageDispatcher
{
    internal sealed class ProfilerMessageSource : IMonitorMessageSource
    {
        private string? _profilerModulePath;

        public event IMonitorMessageSource.MonitorMessageHandler? MonitorMessageEvent;

        public delegate int ProfilerMessageCallback(ProfilerPayloadType payloadType, ProfilerMessageType messageType, IntPtr nativeBuffer, long bufferSize);

        [DllImport(ProfilerIdentifiers.LibraryRootFileName, CallingConvention = CallingConvention.StdCall, PreserveSig = false)]
        private static extern void RegisterMonitorMessageCallback(ProfilerMessageCallback callback);

        private static ProfilerMessageSource? _instance;

        public ProfilerMessageSource()
        {
            _profilerModulePath = Environment.GetEnvironmentVariable(ProfilerIdentifiers.EnvironmentVariables.ModulePath);
            if (!File.Exists(_profilerModulePath))
            {
                throw new FileNotFoundException(_profilerModulePath);
            }

            NativeLibrary.SetDllImportResolver(typeof(MonitorMessageDispatcher).Assembly, ResolveDllImport);
            RegisterMonitorMessageCallback(OnProfilerMessage);
        }

        private void RaiseMonitorMessage(MonitorMessageArgs e)
        {
            MonitorMessageEvent?.Invoke(this, e);
        }

        private IntPtr ResolveDllImport(string libraryName, Assembly assembly, DllImportSearchPath? searchPath)
        {
            // DllImport for Windows automatically loads in-memory modules (such as the profiler). This is not the case for Linux/MacOS.
            // If we fail resolving the DllImport, we have to load the profiler ourselves.
            if (_profilerModulePath == null ||
                libraryName != ProfilerIdentifiers.LibraryRootFileName)
            {
                return IntPtr.Zero;
            }

            if (NativeLibrary.TryLoad(_profilerModulePath, out IntPtr handle))
            {
                return handle;
            }

            return IntPtr.Zero;
        }

        private static int OnProfilerMessage(ProfilerPayloadType payloadType, ProfilerMessageType messageType, IntPtr nativeBuffer, long bufferSize)
        {
            try
            {
                if (bufferSize == 0)
                {
                    throw new ArgumentException(nameof(bufferSize));
                }

                if (nativeBuffer == IntPtr.Zero)
                {
                    throw new ArgumentException(nameof(nativeBuffer));
                }

                ProfilerMessageSource instance = _instance ?? throw new NotSupportedException();
                instance.RaiseMonitorMessage(new MonitorMessageArgs(payloadType, messageType, nativeBuffer, bufferSize));
            }
            catch (Exception ex)
            {
                return Marshal.GetHRForException(ex);
            }

            return 0;
        }

        public void Dispose()
        {
            _instance = null;
        }
    }
}
