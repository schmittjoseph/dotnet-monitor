// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using Microsoft.Extensions.FileProviders;
using System.Threading;
using System.Threading.Tasks;

namespace Microsoft.Diagnostics.Tools.Monitor.StartupHook
{
    internal abstract class AbstractStartupHookService :
        IDiagnosticLifetimeService
    {
        private readonly string _tfm;
        private readonly string _fileName;
        private readonly bool _required;

        private readonly StartupHookApplicator _startupHookApplicator;
        private readonly TaskCompletionSource<bool> _applicationCompletionSource = new(TaskCreationOptions.RunContinuationsAsynchronously);

        public Task<bool> Applied => _applicationCompletionSource.Task;

        public AbstractStartupHookService(
            string tfm,
            string fileName,
            bool required,
            StartupHookApplicator startupHookApplicator)
        {
            _tfm = tfm;
            _fileName = fileName;
            _required = required;

            _startupHookApplicator = startupHookApplicator;
        }


        protected abstract void OnUnableToApply(IFileInfo fileInfo);

        public async ValueTask StartAsync(CancellationToken cancellationToken)
        {
            if (!_required)
            {
                _applicationCompletionSource.TrySetResult(false);
                return;
            }

            (bool applied, IFileInfo fileInfo) = await _startupHookApplicator.ApplyAsync(_tfm, _fileName, cancellationToken);

            if (!applied)
            {
                OnUnableToApply(fileInfo);
            }
            _applicationCompletionSource.TrySetResult(applied);
        }

        public ValueTask StopAsync(CancellationToken cancellationToken)
        {
            return ValueTask.CompletedTask;
        }
    }
}
