// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using Microsoft.Extensions.Logging;
using System.Diagnostics;
using System.Threading;
using System.Threading.Channels;

namespace Microsoft.Diagnostics.Monitoring.HostingStartup.ParameterCapturing.FunctionProbes
{
    internal sealed class LogEmittingProbes : IFunctionProbes
    {
        private readonly ILogger _logger;
        private Thread _loggingThread;

        private Channel<(string, string[])> _messages;

        private AsyncLocal<bool> _eventProcessor = new();

        public LogEmittingProbes(ILogger logger)
        {
            _logger = logger;
            _loggingThread = new Thread(LoggingThread);

            _messages = Channel.CreateBounded<(string, string[])>(new BoundedChannelOptions(100)
            {
                SingleReader = true,
                SingleWriter = false,
                FullMode = BoundedChannelFullMode.DropWrite
            });

            _loggingThread.Start();
        }

        private async void LoggingThread()
        {
            _eventProcessor.Value = true;
            var reader = _messages.Reader;

            await foreach(var m in reader.ReadAllAsync())
            {
                _logger.Log(LogLevel.Warning, m.Item1, m.Item2);
            }
        }

        public void EnterProbe(ulong uniquifier, object[] args)
        {
            if (_eventProcessor.Value == true)
            {
                return;
            }
            // check thread
            if (args == null ||
                FunctionProbesStub.InstrumentedMethodCache?.TryGetValue(uniquifier, out InstrumentedMethod? instrumentedMethod) != true ||
                args.Length != instrumentedMethod?.SupportedParameters.Length)
            {
                return;
            }

            Debug.WriteLine(instrumentedMethod.MethodWithParametersTemplateString);


            string[] argValues = new string[instrumentedMethod.NumberOfSupportedParameters];
            int fmtIndex = 0;
            for (int i = 0; i < args.Length; i++)
            {
                if (!instrumentedMethod.SupportedParameters[i])
                {
                    continue;
                }

                argValues[fmtIndex++] = PrettyPrinter.FormatObject(args[i]);
            }

            // _logger.Log(LogLevel.Warning, instrumentedMethod.MethodWithParametersTemplateString, argValues);
            _messages.Writer.TryWrite((instrumentedMethod.MethodWithParametersTemplateString, argValues));
            return;
        }
    }
}
