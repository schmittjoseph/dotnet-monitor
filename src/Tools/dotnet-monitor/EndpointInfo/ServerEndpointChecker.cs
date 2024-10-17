﻿// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using Microsoft.Diagnostics.Monitoring.WebApi;
using System;
using System.Threading;
using System.Threading.Tasks;

namespace Microsoft.Diagnostics.Tools.Monitor
{
    internal class ServerEndpointChecker(OperationTrackerService operationTracker) : IServerEndpointChecker
    {
        // The amount of time to wait when checking if the a endpoint info should be
        // pruned from the list of endpoint infos. If the runtime doesn't have a viable connection within
        // this time, it will be pruned from the list.
        private static readonly TimeSpan PruneWaitForConnectionTimeout = TimeSpan.FromMilliseconds(250);

        public async Task<EndpointRemovalReason?> CheckEndpointAsync(IEndpointInfo info, CancellationToken token)
        {
            // If a dump operation is in progress, the runtime is likely to not respond to
            // diagnostic requests. Do not check for responsiveness while the dump operation
            // is in progress.
            if (operationTracker.IsExecutingOperation(info))
            {
                return null;
            }

            using var timeoutSource = new CancellationTokenSource();
            using var linkedSource = CancellationTokenSource.CreateLinkedTokenSource(token, timeoutSource.Token);

            try
            {
                timeoutSource.CancelAfter(PruneWaitForConnectionTimeout);

                await info.Endpoint.WaitForConnectionAsync(linkedSource.Token).ConfigureAwait(false);
            }
            catch (OperationCanceledException) when (timeoutSource.IsCancellationRequested)
            {
                return EndpointRemovalReason.Timeout;
            }
            catch (Exception ex) when (ex is not OperationCanceledException)
            {
                return EndpointRemovalReason.Unknown;
            }

            return null;
        }
    }
}
