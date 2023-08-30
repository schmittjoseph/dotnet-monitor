// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using Microsoft.Diagnostics.Monitoring.HostingStartup.ParameterCapturing.ObjectFormatter;
using Microsoft.Diagnostics.Monitoring.TestCommon;
using SampleMethods;
using System;
using Xunit;
using Xunit.Abstractions;

namespace Microsoft.Diagnostics.Monitoring.HostingStartup.UnitTests.ParameterCapturing
{
    [TargetFrameworkMonikerTrait(TargetFrameworkMonikerExtensions.CurrentTargetFrameworkMoniker)]
    public class ObjectFormatterTests
    {
        private readonly ITestOutputHelper _outputHelper;

        public ObjectFormatterTests(ITestOutputHelper outputHelper)
        {
            _outputHelper = outputHelper;
        }

        [Theory]
        [InlineData("test", "'test'")]
        [InlineData(5, "5")]
        [InlineData(true, "True")]
        [InlineData(MyEnum.ValueA, nameof(MyEnum.ValueA))]
        public void GetFormatter_ReturnsCorrectFormatter(object obj, string expected)
        {
            // Arrange
            var formatter = ObjectFormatter.GetFormatter(obj.GetType(), useDebuggerDisplayAttribute: false);

            // Act
            string actual = formatter.Formatter(obj);

            // Assert
            Assert.Equal(expected, actual);
        }
    }
}
