// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using Microsoft.Diagnostics.Tools.Monitor.LibrarySharing;
using Microsoft.Extensions.FileProviders;
using System.Threading;
using System.Threading.Tasks;

namespace Microsoft.Diagnostics.Tools.Monitor.StartupHook
{
    internal sealed class StartupHookFileProvider
    {
        private readonly ISharedLibraryService _sharedLibraryService;

        public StartupHookFileProvider(
            ISharedLibraryService sharedLibraryService)
        {
            _sharedLibraryService = sharedLibraryService;
        }

        public async Task<IFileInfo> GetFileInfoAsync(string tfm, string fileName, CancellationToken token)
        {
            IFileProviderFactory fileProviderFactory = await _sharedLibraryService.GetFactoryAsync(token);
            IFileProvider managedFileProvider = fileProviderFactory.CreateManaged(tfm);

            return managedFileProvider.GetFileInfo(fileName);
        }
    }
}
