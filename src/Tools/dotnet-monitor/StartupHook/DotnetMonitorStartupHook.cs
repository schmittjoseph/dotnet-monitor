// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using Microsoft.Diagnostics.Monitoring.WebApi;
using Microsoft.Extensions.FileProviders;
using Microsoft.Extensions.Logging;
using System.Threading.Tasks;

namespace Microsoft.Diagnostics.Tools.Monitor.StartupHook
{
    public sealed class DotnetMonitorStartupHook : IStartupHook
    {
        private readonly ILogger _logger;
        private readonly IInProcessFeatures _inProcessFeatures;
        private readonly TaskCompletionSource<bool> _applied = new(TaskCreationOptions.RunContinuationsAsynchronously);

        public DotnetMonitorStartupHook(
            ILogger<DotnetMonitorStartupHook> logger,
            IInProcessFeatures inProcessFeatures)
        {
            _logger = logger;
            _inProcessFeatures = inProcessFeatures;
        }

        // Intent is to ship a single TFM of the startup hook, which should be the lowest supported version.
        // If necessary, the startup hook should dynamically access APIs for higher version TFMs and handle
        // all exceptions appropriately.
        public string Tfm => "net6.0";

        public string FileName => "Microsoft.Diagnostics.Monitoring.StartupHook.dll";

        public bool Required => _inProcessFeatures.IsStartupHookRequired;

        public Task<bool> Applied => _applied.Task;

        public void SetIsApplied(bool applied, IFileInfo fileInfo)
        {
            if (_applied.TrySetResult(applied) && Required && !applied)
            {
                _logger.StartupHookInstructions(fileInfo.PhysicalPath);
            }
        }
    }
}
