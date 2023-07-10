﻿// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

namespace Microsoft.Diagnostics.Monitoring.WebApi
{
    /// <summary>
    /// Factory for creating operations that produce dump artifacts.
    /// </summary>
    internal interface ICaptureParametersOperationFactory
    {
        /// <summary>
        /// Creates an operation that captures parameters.
        /// </summary>
        IInProcessOperation Create(
            IEndpointInfo endpointInfo,
            MethodDescription[] methods);
    }
}
