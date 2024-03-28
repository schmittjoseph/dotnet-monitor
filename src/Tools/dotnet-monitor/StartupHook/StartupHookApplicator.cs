// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using Microsoft.Diagnostics.Monitoring.WebApi;
using Microsoft.Diagnostics.NETCore.Client;
using Microsoft.Extensions.FileProviders;
using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;

namespace Microsoft.Diagnostics.Tools.Monitor.StartupHook
{
    internal sealed class StartupHookApplicator
    {
        private readonly ILogger _logger;
        private readonly IEndpointInfo _endpointInfo;

        public StartupHookApplicator(
            ILogger<StartupHookApplicator> logger,
            IEndpointInfo endpointInfo)
        {
            _logger = logger;
            _endpointInfo = endpointInfo;
        }

        public async Task<bool> ApplyAsync(IFileInfo fileInfo, CancellationToken token)
        {
            DiagnosticsClient client = new(_endpointInfo.Endpoint);
            IDictionary<string, string> env = await client.GetProcessEnvironmentAsync(token);

            if (IsEnvironmentConfiguredForStartupHook(fileInfo, env))
            {
                return true;
            }

            return await ApplyStartupHookAsync(fileInfo, _endpointInfo, client, token);
        }

        private async Task<bool> ApplyStartupHookAsync(IFileInfo fileInfo, IEndpointInfo endpointInfo, DiagnosticsClient client, CancellationToken token)
        {
            if (endpointInfo.RuntimeVersion.Major < 8)
            {
                return false;
            }

            try
            {
                await client.ApplyStartupHookAsync(fileInfo.PhysicalPath, token);

                return true;
            }
            catch (Exception ex)
            {
                _logger.StartupHookApplyFailed(ex);
                return false;
            }
        }

        private static bool IsEnvironmentConfiguredForStartupHook(IFileInfo fileInfo, IDictionary<string, string> env)
            => env.TryGetValue(ToolIdentifiers.EnvironmentVariables.StartupHooks, out string startupHookPaths) &&
            startupHookPaths?.Contains(fileInfo.Name, StringComparison.OrdinalIgnoreCase) == true;
    }
}
