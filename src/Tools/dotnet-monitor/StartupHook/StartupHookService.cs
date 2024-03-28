// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using Microsoft.Diagnostics.Monitoring.WebApi;
using Microsoft.Extensions.FileProviders;
using Microsoft.Extensions.Logging;
using System.Threading;
using System.Threading.Tasks;

namespace Microsoft.Diagnostics.Tools.Monitor.StartupHook
{
    internal sealed class StartupHookService : IDiagnosticLifetimeService
    {
        // Intent is to ship a single TFM of the startup hook, which should be the lowest supported version.
        // If necessary, the startup hook should dynamically access APIs for higher version TFMs and handle
        // all exceptions appropriately.
        private const string Tfm = "net6.0";
        private const string FileName = "Microsoft.Diagnostics.Monitoring.StartupHook.dll";

        private readonly ILogger _logger;
        private readonly IEndpointInfo _endpointInfo;
        private readonly IInProcessFeatures _inProcessFeatures;
        private readonly StartupHookFileProvider _startupHookFileProvider;
        private readonly StartupHookApplicator _startupHookApplicator;
        private readonly TaskCompletionSource<bool> _appliedSource = new(TaskCreationOptions.RunContinuationsAsynchronously);

        public Task<bool> Applied => _appliedSource.Task;

        public StartupHookService(
            ILogger<StartupHookService> logger,
            IEndpointInfo endpointInfo,
            IInProcessFeatures inProcessFeatures,
            StartupHookFileProvider startupHookFileProvider,
            StartupHookApplicator startupHookApplicator)
        {
            _logger = logger;
            _endpointInfo = endpointInfo;
            _inProcessFeatures = inProcessFeatures;
            _startupHookFileProvider = startupHookFileProvider;
            _startupHookApplicator = startupHookApplicator;
        }

        public async ValueTask StartAsync(CancellationToken cancellationToken)
        {
            if (!_inProcessFeatures.IsStartupHookRequired)
            {
                _appliedSource.TrySetResult(false);
                return;
            }

            IFileInfo fileInfo = await _startupHookFileProvider.GetFileInfoAsync(Tfm, FileName, cancellationToken);

            bool applied = await _startupHookApplicator.ApplyAsync(fileInfo, cancellationToken);
            if (!applied)
            {
                _logger.StartupHookInstructions(_endpointInfo.ProcessId, fileInfo.PhysicalPath);
            }

            _appliedSource.TrySetResult(applied);
        }

        public ValueTask StopAsync(CancellationToken cancellationToken)
        {
            return ValueTask.CompletedTask;
        }
    }
}
