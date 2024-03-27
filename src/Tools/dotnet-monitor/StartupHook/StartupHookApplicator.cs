// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using Microsoft.Diagnostics.Monitoring.WebApi;
using Microsoft.Diagnostics.NETCore.Client;
using Microsoft.Diagnostics.Tools.Monitor.LibrarySharing;
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
        private readonly ISharedLibraryService _sharedLibraryService;

        public StartupHookApplicator(
            ILogger<StartupHookApplicator> logger,
            IEndpointInfo endpointInfo,
            ISharedLibraryService sharedLibraryService)
        {
            _logger = logger;
            _endpointInfo = endpointInfo;
            _sharedLibraryService = sharedLibraryService;
        }

        public async Task<(bool, IFileInfo)> ApplyAsync(string tfm, string fileName, CancellationToken token)
        {
            DiagnosticsClient client = new(_endpointInfo.Endpoint);
            IDictionary<string, string> env = await client.GetProcessEnvironmentAsync(token);
            IFileInfo fileInfo = await GetStartupHookLibraryFileInfo(tfm, fileName, token);

            if (CheckEnvironmentForStartupHook(fileName, env))
            {
                return (true, fileInfo);
            }

            if (await ApplyStartupHookAsync(fileInfo, _endpointInfo, token))
            {
                return (true, fileInfo);
            }

            return (false, fileInfo);
        }

        private async Task<bool> ApplyStartupHookAsync(IFileInfo fileInfo, IEndpointInfo endpointInfo, CancellationToken token)
        {
            if (endpointInfo.RuntimeVersion.Major < 8)
            {
                return false;
            }

            try
            {
                DiagnosticsClient client = new(endpointInfo.Endpoint);

                await client.ApplyStartupHookAsync(fileInfo.PhysicalPath, token);

                return true;
            }
            catch (Exception ex)
            {
                _logger.StartupHookApplyFailed(ex);
                return false;
            }
        }


        private static bool CheckEnvironmentForStartupHook(string fileName, IDictionary<string, string> env)
        {
            return
                env.TryGetValue(ToolIdentifiers.EnvironmentVariables.StartupHooks, out string startupHookPaths) &&
                !string.IsNullOrEmpty(startupHookPaths) &&
                startupHookPaths.Contains(fileName, StringComparison.OrdinalIgnoreCase);
        }

        private async Task<IFileInfo> GetStartupHookLibraryFileInfo(string tfm, string fileName, CancellationToken token)
        {
            IFileProviderFactory fileProviderFactory = await _sharedLibraryService.GetFactoryAsync(token);

            IFileProvider managedFileProvider = fileProviderFactory.CreateManaged(tfm);

            return managedFileProvider.GetFileInfo(fileName);
        }
    }
}
