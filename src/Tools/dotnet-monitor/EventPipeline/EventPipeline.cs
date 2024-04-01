// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using Microsoft.Diagnostics.Monitoring.WebApi;
using Microsoft.Diagnostics.Monitoring.EventPipe;
using System;
using Microsoft.Diagnostics.NETCore.Client;
using Microsoft.Diagnostics.Tracing;
using System.Threading.Tasks;
using System.Threading;

namespace Microsoft.Diagnostics.Tools.Monitor.EventPipelineWrapper
{
    public abstract class EventPipeline : IAsyncDisposable
    {
        private sealed class PipelineSettings : EventSourcePipelineSettings { }

        private sealed class EventPipelineWrapper : EventSourcePipeline<PipelineSettings>
        {
            private readonly Func<MonitoringSourceConfiguration> _createConfiguration;
            private readonly Func<EventPipeEventSource, Func<Task>, CancellationToken, Task> _onEventSourceAvailable;

            public EventPipelineWrapper(IpcEndpoint endpoint, Func<MonitoringSourceConfiguration> createConfiguration, Func<EventPipeEventSource, Func<Task>, CancellationToken, Task> onEventSourceAvailable, PipelineSettings settings)
                : base(new DiagnosticsClient(endpoint), settings)
            {
                _createConfiguration = createConfiguration;
                _onEventSourceAvailable = onEventSourceAvailable;
            }

            protected override MonitoringSourceConfiguration CreateConfiguration()
            {
                return _createConfiguration();
            }

            protected override Task OnEventSourceAvailable(EventPipeEventSource eventSource, Func<Task> stopSessionAsync, CancellationToken token)
            {
                return _onEventSourceAvailable(eventSource, stopSessionAsync, token);
            }
        }

        private readonly EventPipelineWrapper _pipeline;

        public EventPipeline(IEndpointInfo endpointInfo, TimeSpan duration)
        {
            _pipeline = new EventPipelineWrapper(endpointInfo.Endpoint, CreateConfiguration, OnEventSourceAvailable, new PipelineSettings()
            {
                Duration = duration
            });
        }

        protected abstract MonitoringSourceConfiguration CreateConfiguration();
        protected abstract Task OnEventSourceAvailable(EventPipeEventSource eventSource, Func<Task> stopSessionAsync, CancellationToken token);

        public Task<Task> StartAsync(CancellationToken token) => _pipeline.StartAsync(token);
        public Task StopAsync(CancellationToken token) => _pipeline.StopAsync(token);
        public ValueTask DisposeAsync() => _pipeline.DisposeAsync();
    }
}
