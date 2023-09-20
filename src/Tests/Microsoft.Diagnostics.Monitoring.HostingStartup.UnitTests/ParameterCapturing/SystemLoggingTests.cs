// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using Microsoft.Diagnostics.Monitoring.HostingStartup.ParameterCapturing;
using Microsoft.Diagnostics.Monitoring.HostingStartup.ParameterCapturing.FunctionProbes;
using Microsoft.Diagnostics.Monitoring.TestCommon;
using Microsoft.Extensions.Logging;
using System;
using System.Linq;
using System.Reflection;
using Xunit;

namespace Microsoft.Diagnostics.Monitoring.HostingStartup.UnitTests.ParameterCapturing
{
    [TargetFrameworkMonikerTrait(TargetFrameworkMonikerExtensions.CurrentTargetFrameworkMoniker)]
    public class SystemLoggingTests
    {
        private static void TestMethod1() { }
        private static void TestMethod2() { }

        [Fact]
        public void TestLoggingCategories()
        {
            var logRecord = new LogRecord();
            var factory = LoggerFactory.Create(builder => builder.AddProvider(new TestLoggerProvider(logRecord)));

            MethodInfo method1 = typeof(SystemLoggingTests).GetMethod(nameof(TestMethod1), BindingFlags.Static | BindingFlags.NonPublic);
            MethodInfo method2 = typeof(SystemLoggingTests).GetMethod(nameof(TestMethod2), BindingFlags.Static | BindingFlags.NonPublic);
            Assert.NotNull(method1);
            Assert.NotNull(method2);

            MethodTemplateString message1 = new(method1);
            MethodTemplateString message2 = new(method2);


            using (ParameterCapturingLogger logger = new(factory.CreateLogger<DotnetMonitor.ParameterCapture.UserCode>(), factory.CreateLogger<DotnetMonitor.ParameterCapture.SystemCode>()))
            {
                logger.Log(ParameterCaptureMode.Inline, message1, Array.Empty<string>());
                logger.Log(ParameterCaptureMode.Background, message2, Array.Empty<string>());

                // Force the logger to drain the background queue before we dispose it.
                logger.Complete();
            }

            Assert.Equal(2, logRecord.Events.Count);

            var userCodeEntry = logRecord.Events.First(e => e.Message == message1.Template);
            Assert.Equal(typeof(DotnetMonitor.ParameterCapture.UserCode).FullName, userCodeEntry.Category);

            var systemEntry = logRecord.Events.First(e => e.Message == message2.Template);
            Assert.Equal(typeof(DotnetMonitor.ParameterCapture.SystemCode).FullName, systemEntry.Category);

            return;
        }
    }
}
