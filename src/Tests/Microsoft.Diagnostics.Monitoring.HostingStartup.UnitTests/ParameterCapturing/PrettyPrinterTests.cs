// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using Microsoft.Diagnostics.Monitoring.HostingStartup.ParameterCapturing;
using Microsoft.Diagnostics.Monitoring.TestCommon;
using System.Reflection;
using System;
using Xunit;
using SampleSignatures;
using Xunit.Abstractions;

namespace Microsoft.Diagnostics.Monitoring.HostingStartup.UnitTests.ParameterCapturing
{
    [TargetFrameworkMonikerTrait(TargetFrameworkMonikerExtensions.CurrentTargetFrameworkMoniker)]
    public class PrettyPrinterTests
    {
        private readonly ITestOutputHelper _outputHelper;

        public PrettyPrinterTests(ITestOutputHelper outputHelper)
        {
            _outputHelper = outputHelper;
        }

        [Theory]
        [InlineData(typeof(TestMethodSignatures), nameof(TestMethodSignatures.ImplicitThis), "SampleSignatures.TestMethodSignatures.ImplicitThis(this: {0})")]
        [InlineData(typeof(StaticTestMethodSignatures), nameof(StaticTestMethodSignatures.RefStruct), "SampleSignatures.StaticTestMethodSignatures.RefStruct(ref myRefStruct: {{unsupported}})")]
        [InlineData(typeof(StaticTestMethodSignatures), nameof(StaticTestMethodSignatures.GenericParameters), "SampleSignatures.StaticTestMethodSignatures.GenericParameters<T, K>(t: {0}, k: {1})")]
        public void MethodFormatString(Type t, string methodName, string formatString)
        {
            // Arrange
            MethodInfo method = t.GetMethod(methodName);

            // Act
            uint[] tokens = BoxingTokens.GetBoxingTokens(method);
            bool[] supportedArgs = BoxingTokens.GetSupportedArgs(tokens);
            string actualFormatString = PrettyPrinter.ConstructFormattableStringFromMethod(method, supportedArgs);
            actualFormatString = actualFormatString.ReplaceLineEndings("").Replace("\t", "");
            _outputHelper.WriteLine(actualFormatString);

            // Assert
            Assert.Equal(formatString, actualFormatString);
        }
    }
}
