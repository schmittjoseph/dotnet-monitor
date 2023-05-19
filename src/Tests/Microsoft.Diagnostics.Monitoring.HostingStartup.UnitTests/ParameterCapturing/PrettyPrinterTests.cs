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
        [InlineData(typeof(StaticTestMethodSignatures), nameof(StaticTestMethodSignatures.Delegate), "SampleSignatures.StaticTestMethodSignatures.Delegate(func: {0})")]
        [InlineData(typeof(StaticTestMethodSignatures), nameof(StaticTestMethodSignatures.BasicTypes), "SampleSignatures.StaticTestMethodSignatures.BasicTypes(s: {0}, intArray: {1}, multidimensionalArray: {2}, uInt: {3})")]
        [InlineData(typeof(StaticTestMethodSignatures), nameof(StaticTestMethodSignatures.RefStruct), "SampleSignatures.StaticTestMethodSignatures.RefStruct(ref myRefStruct: {{unsupported}})")]
        [InlineData(typeof(StaticTestMethodSignatures), nameof(StaticTestMethodSignatures.GenericParameters), "SampleSignatures.StaticTestMethodSignatures.GenericParameters<T, K>(t: {0}, k: {1})")]
        [InlineData(typeof(StaticTestMethodSignatures), nameof(StaticTestMethodSignatures.Unicode_ΦΨ), "SampleSignatures.StaticTestMethodSignatures.Unicode_ΦΨ(δ: {0})")]
        public void MethodFormatString(Type t, string methodName, string formatString)
        {
            // Arrange
            MethodInfo method = t.GetMethod(methodName);
            Assert.NotNull(method);

            // Act
            uint[] tokens = BoxingTokens.GetBoxingTokens(method);
            bool[] supportedArgs = BoxingTokens.GetSupportedArgs(tokens);
            string actualFormatString = PrettyPrinter.ConstructFormattableStringFromMethod(method, supportedArgs);

            // Assert
            Assert.NotNull(actualFormatString);
            actualFormatString = actualFormatString.ReplaceLineEndings("").Replace("\t", "");
            _outputHelper.WriteLine(actualFormatString);
            Assert.Equal(formatString, actualFormatString);
        }

        [Theory]
        [InlineData(null, "null")]
        [InlineData("test", "'test'")]
        [InlineData(5, "5")]
        [InlineData(MyEnum.ValueA, nameof(MyEnum.ValueA))]
        public void SerializeObject(object obj, string value)
        {
            // Act
            string actualValue = PrettyPrinter.SerializeObject(obj);

            // Assert
            Assert.Equal(value, actualValue);
        }
    }
}
