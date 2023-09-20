// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using Microsoft.Diagnostics.Monitoring.HostingStartup.ParameterCapturing.FunctionProbes;
using Microsoft.Diagnostics.Monitoring.StartupHook;
using Microsoft.Extensions.Logging;
using System;
using System.Collections.Concurrent;
using System.Diagnostics;
using System.Linq;
using System.Threading;

namespace Microsoft.Diagnostics.Monitoring.HostingStartup.ParameterCapturing
{
    internal sealed class ParameterCapturingLogger : IDisposable
    {
        private static class Scopes
        {
            private const string Prefix = "DotnetMonitor_";

            public const string TimeStamp = Prefix + "Timestamp";
            public const string ActivityId = Prefix + "ActivityId";
            public const string ActivityIdFormat = Prefix + "ActivityIdFormat";


            public static class CaptureSite
            {
                private const string Prefix = Scopes.Prefix + "CaptureSite_";

                public const string MethodName = Prefix + "MethodName";

                public const string ModuleName = Prefix + "ModuleName";
                public const string DeclaringTypeName = Prefix + "DeclaringTypeName";
            }
        }

        private readonly ILogger _userLogger;
        private readonly ILogger _systemLogger;
        private readonly Thread _thread;
        private BlockingCollection<(string format, string[] args, KeyValueLogScope scope)> _messages;
        private uint _droppedMessageCounter;
        private const int BackgroundLoggingCapacity = 1024;
        private const string BackgroundLoggingThreadName = "[dotnet-monitor] Probe Logging Thread";
        private long _disposedState;

        private static readonly string[] ExcludedThreads = new[]
        {
            "Console logger queue processing thread",
        };

        public ParameterCapturingLogger(ILogger userLogger, ILogger systemLogger)
        {
            _userLogger = userLogger;
            _systemLogger = systemLogger;
            _thread = new Thread(ThreadProc);

            _thread.Priority = ThreadPriority.BelowNormal;
            _thread.IsBackground = true;
            _thread.Name = BackgroundLoggingThreadName;
            _messages = new BlockingCollection<(string, string[], KeyValueLogScope)>(BackgroundLoggingCapacity);
            _thread.Start();
        }

        public bool ShouldLog()
        {
            // Probes should not attempt to log on the console logging thread
            // or on the background thread that is used to log system messages.

            if (Environment.CurrentManagedThreadId == _thread.ManagedThreadId)
            {
                return false;
            }
            if (ExcludedThreads.Contains(Thread.CurrentThread.Name))
            {
                return false;
            }

            return true;
        }

        public void Log(ParameterCaptureMode mode, MethodTemplateString methodTemplateString, string[] args)
        {
            DisposableHelper.ThrowIfDisposed<ParameterCapturingLogger>(ref _disposedState);

            // Construct scope
            KeyValueLogScope scope = GenerateScope(methodTemplateString);

            if (mode == ParameterCaptureMode.Inline)
            {
                Log(_userLogger, methodTemplateString.TemplateString, args, scope);
            }
            else if (mode == ParameterCaptureMode.Background)
            {
                if (!_messages.TryAdd((methodTemplateString.TemplateString, args, scope)))
                {
                    Interlocked.Increment(ref _droppedMessageCounter);
                }
            }
        }

        private static KeyValueLogScope GenerateScope(MethodTemplateString methodTemplateString)
        {
            KeyValueLogScope scope = new();

            // Store timestamp as ISO 8601
            scope.Values.Add(Scopes.TimeStamp, DateTime.UtcNow.ToString("o"));

            scope.Values.Add(Scopes.CaptureSite.ModuleName, methodTemplateString.ModuleName);
            scope.Values.Add(Scopes.CaptureSite.DeclaringTypeName, methodTemplateString.TypeName);
            scope.Values.Add(Scopes.CaptureSite.MethodName, methodTemplateString.MethodName);

            Activity? currentActivity = Activity.Current;
            if (currentActivity?.Id != null)
            {
                scope.Values.Add(Scopes.ActivityId, currentActivity.Id);
                scope.Values.Add(Scopes.ActivityIdFormat, currentActivity.IdFormat);
            }

            return scope;
        }

        private void ThreadProc()
        {
            try
            {
                while (_messages.TryTake(out (string format, string[] args, KeyValueLogScope scope) entry, Timeout.InfiniteTimeSpan))
                {
                    Log(_systemLogger, entry.format, entry.args, entry.scope);
                }
            }
            catch (ObjectDisposedException)
            {
            }
            catch
            {
            }
        }

        public void Complete()
        {
            // NOTE We currently do not wait for the background thread in production code
            _messages.CompleteAdding();
            _thread.Join();
        }

        private static void Log(ILogger logger, string format, string[] args, KeyValueLogScope scope)
        {
            using var _ = logger.BeginScope(scope);
            logger.Log(LogLevel.Information, format, args);
        }

        public void Dispose()
        {
            if (!DisposableHelper.CanDispose(ref _disposedState))
            {
                return;
            }
            _messages.CompleteAdding();
            _messages.Dispose();
        }
    }
}
