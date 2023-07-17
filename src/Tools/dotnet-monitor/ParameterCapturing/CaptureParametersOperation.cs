// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using Microsoft.Diagnostics.Monitoring;
using Microsoft.Diagnostics.Monitoring.WebApi;
using Microsoft.Diagnostics.Monitoring.WebApi.Models;
using Microsoft.Extensions.Logging;
using System;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

namespace Microsoft.Diagnostics.Tools.Monitor.ParameterCapturing
{
    internal sealed class CaptureParametersOperation : IInProcessOperation
    {
        private readonly ProfilerChannel _profilerChannel;
        private readonly IEndpointInfo _endpointInfo;
        private readonly ILogger _logger;
        private readonly MethodDescription[] _methods;
        private readonly TimeSpan _duration;

        public CaptureParametersOperation(IEndpointInfo endpointInfo, ProfilerChannel profilerChannel, ILogger logger, MethodDescription[] methods, TimeSpan duration)
        {
            _profilerChannel = profilerChannel;
            _endpointInfo = endpointInfo;
            _logger = logger;
            _methods = methods;
            _duration = duration;
        }

        public async Task ExecuteAsync(TaskCompletionSource<object> startCompletionSource, CancellationToken token)
        {
            var settings = new EventParameterCapturingPipelineSettings
            {
                Duration = Timeout.InfiniteTimeSpan
            };


            TaskCompletionSource<bool> stoppedCompletionSource = new(TaskCreationOptions.RunContinuationsAsynchronously);
            TaskCompletionSource<bool> ackCompletionSource = new(TaskCreationOptions.RunContinuationsAsynchronously);
            TaskCompletionSource<int[]> unableToResolveMethodsCompletionSource = new(TaskCreationOptions.RunContinuationsAsynchronously);

            await using EventParameterCapturingPipeline eventTracePipeline = new(_endpointInfo.Endpoint, settings);
            eventTracePipeline.OnStartedCapturing += (_, _) =>
            {
                ackCompletionSource.TrySetResult(true);
            };
            eventTracePipeline.OnStoppedCapturing += (_, _) =>
            {
                stoppedCompletionSource.TrySetResult(true);
            };
            eventTracePipeline.OnUnableToResolveMethods += (_, unresolvedInidices) =>
            {
                unableToResolveMethodsCompletionSource.TrySetResult(unresolvedInidices);
            };

            Task runPipelineTask = eventTracePipeline.StartAsync(token);

            await _profilerChannel.SendMessage(
                _endpointInfo,
                new JsonProfilerMessage(IpcCommand.StartCapturingParameters, new StartCapturingParametersPayload
                {
                    Duration = _duration,
                    Methods = _methods
                }),
                token);

            startCompletionSource?.TrySetResult(null);

            token.Register(async () =>
            {
                await StopAsync(CancellationToken.None);
            });


            // unwrap..
            await await Task.WhenAny(
                ackCompletionSource.Task,
                unableToResolveMethodsCompletionSource.Task
                ).WaitAsync(token).ConfigureAwait(false);

            if (unableToResolveMethodsCompletionSource.Task.IsCompleted)
            {
                int[] indicies = await unableToResolveMethodsCompletionSource.Task;
                StringBuilder unresolvedMethods = new();
                foreach (int i in indicies)
                {
                    unresolvedMethods.Append(_methods[i]);
                }
                throw new ArgumentException($"Could not resolve all methods: {unresolvedMethods}");
            }

            await stoppedCompletionSource.Task.WaitAsync(token).ConfigureAwait(false);
        }

        public async Task StopAsync(CancellationToken token)
        {
            await _profilerChannel.SendMessage(
                _endpointInfo,
                new JsonProfilerMessage(IpcCommand.StopCapturingParameters, new EmptyPayload()),
                token);
        }

        public bool IsStoppable => true;

        public string Description => "Parameter Capturing";
    }
}
