// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using Microsoft.Extensions.FileProviders;
using System.Threading.Tasks;

namespace Microsoft.Diagnostics.Tools.Monitor.StartupHook
{
    internal interface IStartupHook
    {
        public Task<bool> Applied { get; }
        public abstract string Tfm { get; }
        public abstract string FileName { get; }
        public abstract bool Required { get; }

        public void SetIsApplied(bool isApplied, IFileInfo fileInfo);

    }
}
