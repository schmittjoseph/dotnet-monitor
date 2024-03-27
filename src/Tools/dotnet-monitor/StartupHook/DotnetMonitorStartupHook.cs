// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using Microsoft.Diagnostics.Monitoring.WebApi;
using Microsoft.Extensions.FileProviders;
using Microsoft.Extensions.Logging;

namespace Microsoft.Diagnostics.Tools.Monitor.StartupHook
{
    internal sealed class DotnetMonitorStartupHook : AbstractStartupHookService
    {
        private readonly ILogger _logger;

        // Intent is to ship a single TFM of the startup hook, which should be the lowest supported version.
        // If necessary, the startup hook should dynamically access APIs for higher version TFMs and handle
        // all exceptions appropriately.
        private const string Tfm = "net6.0";
        private const string FileName = "Microsoft.Diagnostics.Monitoring.StartupHook.dll";

        public DotnetMonitorStartupHook(
            ILogger<DotnetMonitorStartupHook> logger,
            StartupHookApplicator startupHookApplicator,
            IInProcessFeatures inProcessFeatures)
            : base(Tfm, FileName, inProcessFeatures.IsStartupHookRequired, startupHookApplicator)
        {
            _logger = logger;
        }

        protected override void OnUnableToApply(IFileInfo fileInfo)
            => _logger.StartupHookInstructions(fileInfo.PhysicalPath);
    }
}
