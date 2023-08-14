﻿// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using System.Threading;
using System.Threading.Tasks;

namespace Microsoft.Diagnostics.Monitoring.WebApi
{
    internal static class TaskCompletionSourceExtensions
    {
        /// <summary>
        /// Creates a <see cref="Task"/> that completes when the <see cref="TaskCompletionSource{TResult}"/> completes
        /// or is cancelled when the <see cref="CancellationToken"/> is signaled.
        /// </summary>
        /// <remarks>
        /// Signaling the <see cref="CancellationToken"/> will cancel the <see cref="TaskCompletionSource{TResult}"/>.
        /// If cancellation of the <see cref="TaskCompletionSource{TResult}"/> is not intended,
        /// use <see cref="TaskExtensions.WithCancellation{T}(Task{T}, CancellationToken)"/>
        /// on the <see cref="TaskCompletionSource{TResult}.Task"/> property.
        /// </remarks>
        public static async Task<T> WithCancellation<T>(this TaskCompletionSource<T> source, CancellationToken token)
        {
            using (token.Register(source => ((TaskCompletionSource<T>)source).TrySetCanceled(token), source))
            {
                return await source.Task.ConfigureAwait(false);
            }
        }

        public static Task MirrorStateOnCompletion<T>(this TaskCompletionSource<T> source, TaskCompletionSource<T> destination, CancellationToken token)
        {
            return source.Task.ContinueWith(async task =>
            {
                if (task.IsCompletedSuccessfully)
                {
                    destination.TrySetResult(await task);
                }
                else if (task.IsCanceled)
                {
                    if (token.IsCancellationRequested)
                    {
                        destination.TrySetCanceled(token);
                    }
                    else
                    {
                        destination.TrySetCanceled();
                    }
                }
                else if (task.IsFaulted)
                {
                    destination.TrySetException(task.Exception);
                }
            }, token, TaskContinuationOptions.RunContinuationsAsynchronously, TaskScheduler.Default);
        }
    }
}
