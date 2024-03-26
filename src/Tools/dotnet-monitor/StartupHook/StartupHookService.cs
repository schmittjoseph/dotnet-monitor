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
    internal sealed class StartupHookService :
        IDiagnosticLifetimeService
    {
        private readonly ILogger _logger;
        private readonly IEndpointInfo _endpointInfo;
        private readonly IEnumerable<IStartupHook> _startupHooks;
        private readonly ISharedLibraryService _sharedLibraryService;

        public StartupHookService(
            ILogger<StartupHookService> logger,
            IEndpointInfo endpointInfo,
            IEnumerable<IStartupHook> startupHooks,
            ISharedLibraryService sharedLibraryService)
        {
            _logger = logger ?? throw new ArgumentNullException(nameof(logger));
            _endpointInfo = endpointInfo ?? throw new ArgumentNullException(nameof(endpointInfo));
            _sharedLibraryService = sharedLibraryService ?? throw new ArgumentNullException(nameof(sharedLibraryService));
            _startupHooks = startupHooks ?? throw new ArgumentNullException(nameof(startupHooks));
        }

        public async ValueTask StartAsync(CancellationToken cancellationToken)
        {
            DiagnosticsClient client = new(_endpointInfo.Endpoint);
            IDictionary<string, string> env = await client.GetProcessEnvironmentAsync(cancellationToken);

            foreach (IStartupHook startupHook in _startupHooks)
            {
                IFileInfo fileInfo = await GetStartupHookLibraryFileInfo(startupHook, cancellationToken);

                if (!startupHook.Required)
                {
                    startupHook.SetIsApplied(false, fileInfo);
                    continue;
                }

                if (await ApplyStartupHookAsync(startupHook, fileInfo, env, _endpointInfo, cancellationToken))
                {
                    startupHook.SetIsApplied(true, fileInfo);
                    return;
                }

                startupHook.SetIsApplied(false, fileInfo);
            }
        }

        private async Task<bool> ApplyStartupHookAsync(IStartupHook startupHook, IFileInfo fileInfo, IDictionary<string, string> env, IEndpointInfo endpointInfo, CancellationToken token)
        {
            if (CheckEnvironmentForStartupHook(startupHook, env))
            {
                return true;
            }

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


        private static bool CheckEnvironmentForStartupHook(IStartupHook startupHook, IDictionary<string, string> env)
        {
            return
                env.TryGetValue(ToolIdentifiers.EnvironmentVariables.StartupHooks, out string startupHookPaths) &&
                !string.IsNullOrEmpty(startupHookPaths) &&
                startupHookPaths.Contains(startupHook.FileName, StringComparison.OrdinalIgnoreCase);
        }

        private async Task<IFileInfo> GetStartupHookLibraryFileInfo(IStartupHook startupHook, CancellationToken token)
        {
            IFileProviderFactory fileProviderFactory = await _sharedLibraryService.GetFactoryAsync(token);

            IFileProvider managedFileProvider = fileProviderFactory.CreateManaged(startupHook.Tfm);

            return managedFileProvider.GetFileInfo(startupHook.FileName);
        }

        public ValueTask StopAsync(CancellationToken cancellationToken)
        {
            return ValueTask.CompletedTask;
        }
    }
}
