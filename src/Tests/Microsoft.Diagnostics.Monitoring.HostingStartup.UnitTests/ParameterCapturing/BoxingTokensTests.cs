// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using Microsoft.Diagnostics.Monitoring.HostingStartup.ParameterCapturing;
using Microsoft.Diagnostics.Monitoring.TestCommon;
using SampleSignatures;
using System;
using System.Reflection;
using Xunit;
using Xunit.Abstractions;

namespace Microsoft.Diagnostics.Monitoring.HostingStartup.UnitTests.ParameterCapturing
{
    [TargetFrameworkMonikerTrait(TargetFrameworkMonikerExtensions.CurrentTargetFrameworkMoniker)]
    public class BoxingTokensTests
    {
        private readonly ITestOutputHelper _outputHelper;

        public BoxingTokensTests(ITestOutputHelper outputHelper)
        {
            _outputHelper = outputHelper;
        }

        [Theory]
        [InlineData(typeof(TestMethodSignatures), nameof(TestMethodSignatures.ImplicitThis), true)]

        [InlineData(typeof(StaticTestMethodSignatures), nameof(StaticTestMethodSignatures.NoArgs))]
        [InlineData(typeof(StaticTestMethodSignatures), nameof(StaticTestMethodSignatures.BasicTypes), true, true, true, true)]
        [InlineData(typeof(StaticTestMethodSignatures), nameof(StaticTestMethodSignatures.ExplicitThis), true)]
        [InlineData(typeof(StaticTestMethodSignatures), nameof(StaticTestMethodSignatures.RefParam), false)]
        [InlineData(typeof(StaticTestMethodSignatures), nameof(StaticTestMethodSignatures.OutParam), false)]
        [InlineData(typeof(StaticTestMethodSignatures), nameof(StaticTestMethodSignatures.RefStruct), false)]
        [InlineData(typeof(StaticTestMethodSignatures), nameof(StaticTestMethodSignatures.Pointer), false)]
        [InlineData(typeof(StaticTestMethodSignatures), nameof(StaticTestMethodSignatures.GenericParameters), true, true)]
        [InlineData(typeof(StaticTestMethodSignatures), nameof(StaticTestMethodSignatures.TypeDef), true)]
        [InlineData(typeof(StaticTestMethodSignatures), nameof(StaticTestMethodSignatures.TypeRef), true)]
        [InlineData(typeof(StaticTestMethodSignatures), nameof(StaticTestMethodSignatures.TypeSpec), true)]
        [InlineData(typeof(StaticTestMethodSignatures), nameof(StaticTestMethodSignatures.ValueTypeDef), true)]
        [InlineData(typeof(StaticTestMethodSignatures), nameof(StaticTestMethodSignatures.ValueTypeRef), false)]
        [InlineData(typeof(StaticTestMethodSignatures), nameof(StaticTestMethodSignatures.ValueTypeSpec), false)]
        [InlineData(typeof(StaticTestMethodSignatures), nameof(StaticTestMethodSignatures.Unicode_ΦΨ), true)]
        public void GetBoxingTokens_HandlesUnsupportedArgs(Type t, string methodName, params bool[] supported)
        {
            // Arrange
            MethodInfo method = t.GetMethod(methodName);

            // Act
            uint[] tokens = BoxingTokens.GetBoxingTokens(method);
            bool[] supportedArgs = BoxingTokens.GetSupportedArgs(tokens);

            // Assert
            Assert.Equal(supported, supportedArgs);
        }
    }
}
