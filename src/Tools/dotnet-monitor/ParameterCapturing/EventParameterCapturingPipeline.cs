// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using Microsoft.Diagnostics.Monitoring.EventPipe;
using Microsoft.Diagnostics.Monitoring.WebApi;
using Microsoft.Diagnostics.NETCore.Client;
using Microsoft.Diagnostics.Tracing;
using System;
using System.Diagnostics.Tracing;
using System.Threading;
using System.Threading.Tasks;

namespace Microsoft.Diagnostics.Tools.Monitor.ParameterCapturing
{
    internal sealed class RemoteException
    {
        public string FailureType { get; set; }
        public string FailureMessage { get; set; }

        public void ReThrow()
        {
            throw new Exception($"[{FailureType}]: {FailureMessage}");
        }
    }

    internal sealed class EventParameterCapturingPipeline : EventSourcePipeline<EventParameterCapturingPipelineSettings>
    {

        public EventHandler OnStartedCapturing;
        public EventHandler OnStoppedCapturing;

        public EventHandler<RemoteException> OnCapturingFailed;

        public EventParameterCapturingPipeline(IpcEndpoint endpoint, EventParameterCapturingPipelineSettings settings)
            : base(new DiagnosticsClient(endpoint), settings)

        {
        }

        protected override MonitoringSourceConfiguration CreateConfiguration()
        {
            return new EventPipeProviderSourceConfiguration(requestRundown: false, bufferSizeInMB: 64, new[]
            {
                new EventPipeProvider(ParameterCapturingEvents.SourceName, EventLevel.Informational, (long)EventKeywords.All)
            });
        }

        protected override Task OnEventSourceAvailable(EventPipeEventSource eventSource, Func<Task> stopSessionAsync, CancellationToken token)
        {
            eventSource.Dynamic.AddCallbackForProviderEvent(
                ParameterCapturingEvents.SourceName,
                null,
                Callback);

            return Task.CompletedTask;
        }

        private void Callback(TraceEvent traceEvent)
        {
            Console.WriteLine(traceEvent.EventName);
            // Using event name instead of event ID because event ID seem to be dynamically assigned
            // in the order in which they are used.
            switch (traceEvent.EventName)
            {
                case "Capturing/Start":
                    OnStartedCapturing.Invoke(this, EventArgs.Empty);
                    break;
                case "Capturing/Stop":
                    OnStoppedCapturing.Invoke(this, EventArgs.Empty);
                    break;
                case "FailedToCapture":
                    string exceptionType = traceEvent.GetPayload<string>(ParameterCapturingEvents.CapturingFailedPayloads.FailureType);
                    string exceptionMessage = traceEvent.GetPayload<string>(ParameterCapturingEvents.CapturingFailedPayloads.FailureMessage);
                    OnCapturingFailed.Invoke(this, new RemoteException()
                    {
                        FailureType = exceptionType,
                        FailureMessage = exceptionMessage
                    });
                    break;
                case "Flush":
                    break;
#if DEBUG
                 default:
                    throw new NotSupportedException("Unhandled event: " + traceEvent.EventName);
#endif
            }
        }

        public new Task StartAsync(CancellationToken token)
        {
            return base.StartAsync(token);
        }
    }

    internal sealed class EventParameterCapturingPipelineSettings : EventSourcePipelineSettings
    {
        public EventParameterCapturingPipelineSettings()
        {
            Duration = Timeout.InfiniteTimeSpan;
        }
    }
}
