// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using Microsoft.Diagnostics.Monitoring;
using Microsoft.Diagnostics.Monitoring.WebApi;
using Microsoft.Extensions.Logging;

namespace Microsoft.Diagnostics.Tools.Monitor.ParameterCapturing
{
    internal sealed class CaptureParametersOperationFactory : ICaptureParametersOperationFactory
    {
        private readonly ProfilerChannel _profilerChannel;
        private readonly ILogger<CaptureParametersOperation> _logger;

        public CaptureParametersOperationFactory(ProfilerChannel profilerChannel, ILogger<CaptureParametersOperation> logger)
        {
            _profilerChannel = profilerChannel;
            _logger = logger;
        }

        public IInProcessOperation Create(IEndpointInfo endpointInfo, MethodDescription[] methods)
        {
            return new CaptureParametersOperation(endpointInfo, _profilerChannel, _logger, methods);
        }
    }
}
