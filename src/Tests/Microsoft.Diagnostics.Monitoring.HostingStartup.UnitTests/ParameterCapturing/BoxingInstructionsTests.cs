﻿// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using Microsoft.Diagnostics.Monitoring.HostingStartup.ParameterCapturing;
using Microsoft.Diagnostics.Monitoring.HostingStartup.ParameterCapturing.FunctionProbes;
using Microsoft.Diagnostics.Monitoring.TestCommon;
using SampleMethods;
using System;
using System.Reflection;
using Xunit;
using Xunit.Abstractions;
using static Microsoft.Diagnostics.Monitoring.HostingStartup.ParameterCapturing.BoxingInstructions;

namespace Microsoft.Diagnostics.Monitoring.HostingStartup.UnitTests.ParameterCapturing
{
    [TargetFrameworkMonikerTrait(TargetFrameworkMonikerExtensions.CurrentTargetFrameworkMoniker)]
    public class BoxingInstructionsTests
    {
        private readonly ITestOutputHelper _outputHelper;

        public BoxingInstructionsTests(ITestOutputHelper outputHelper)
        {
            _outputHelper = outputHelper;
        }

        [Theory]
        [InlineData(typeof(TestMethodSignatures), nameof(TestMethodSignatures.ImplicitThis), true)]
        [InlineData(typeof(StaticTestMethodSignatures.SampleNestedStruct), nameof(StaticTestMethodSignatures.SampleNestedStruct.DoWork), false, true)]
        [InlineData(typeof(StaticTestMethodSignatures), nameof(StaticTestMethodSignatures.NoArgs))]
        [InlineData(typeof(StaticTestMethodSignatures), nameof(StaticTestMethodSignatures.Arrays), true, true)]
        [InlineData(typeof(StaticTestMethodSignatures), nameof(StaticTestMethodSignatures.BuiltInReferenceTypes), true, true, true)]
        [InlineData(typeof(StaticTestMethodSignatures), nameof(StaticTestMethodSignatures.Delegate), true)]
        [InlineData(typeof(StaticTestMethodSignatures), nameof(StaticTestMethodSignatures.ExplicitThis), true)]
        [InlineData(typeof(StaticTestMethodSignatures), nameof(StaticTestMethodSignatures.InParam), false)]
        [InlineData(typeof(StaticTestMethodSignatures), nameof(StaticTestMethodSignatures.RefParam), false)]
        [InlineData(typeof(StaticTestMethodSignatures), nameof(StaticTestMethodSignatures.OutParam), false)]
        [InlineData(typeof(StaticTestMethodSignatures), nameof(StaticTestMethodSignatures.RefStruct), false)]
        [InlineData(typeof(StaticTestMethodSignatures), nameof(StaticTestMethodSignatures.RecordStruct), true)]
        [InlineData(typeof(StaticTestMethodSignatures), nameof(StaticTestMethodSignatures.NativeIntegers), true, true)]
        [InlineData(typeof(StaticTestMethodSignatures), nameof(StaticTestMethodSignatures.Pointer), false)]
        [InlineData(typeof(StaticTestMethodSignatures), nameof(StaticTestMethodSignatures.GenericParameters), false, false)]
        [InlineData(typeof(StaticTestMethodSignatures), nameof(StaticTestMethodSignatures.TypeDef), true)]
        [InlineData(typeof(StaticTestMethodSignatures), nameof(StaticTestMethodSignatures.TypeRef), true)]
        [InlineData(typeof(StaticTestMethodSignatures), nameof(StaticTestMethodSignatures.TypeSpec), true)]
        [InlineData(typeof(StaticTestMethodSignatures), nameof(StaticTestMethodSignatures.ValueType_TypeDef), true)]
        [InlineData(typeof(StaticTestMethodSignatures), nameof(StaticTestMethodSignatures.ValueType_TypeRef), true)]
        [InlineData(typeof(StaticTestMethodSignatures), nameof(StaticTestMethodSignatures.ValueType_TypeSpec), false)]
        [InlineData(typeof(StaticTestMethodSignatures), nameof(StaticTestMethodSignatures.VarArgs), true, true)]
        [InlineData(typeof(StaticTestMethodSignatures), nameof(StaticTestMethodSignatures.Unicode_ΦΨ), true)]
        public void GetBoxingInstructions_Detects_UnsupportedParameters(Type declaringType, string methodName, params bool[] supported)
        {
            // Arrange
            MethodInfo method = declaringType.GetMethod(methodName);

            // Act
            bool[] supportedParameters = BoxingInstructions.AreParametersSupported(BoxingInstructions.GetBoxingInstructions(method));

            // Assert
            Assert.Equal(supported, supportedParameters);
        }

        [Fact]
        public void GetBoxingInstructions_Detects_UnsupportedGenericParameters()
        {
            // Arrange
            MethodInfo method = Type.GetType($"{nameof(SampleMethods)}.GenericTestMethodSignatures`2").GetMethod("GenericParameters");
            bool[] supported = new bool[] { true, false, false, false };

            // Act
            bool[] supportedParameters = BoxingInstructions.AreParametersSupported(BoxingInstructions.GetBoxingInstructions(method));

            // Assert
            Assert.Equal(supported, supportedParameters);
        }

        [Fact]
        public void GetBoxingInstructions_Handles_Primitives()
        {
            // Arrange
            ParameterBoxingInstructions[] expectedInstructions = [
                SpecialCaseBoxingTypes.Boolean,
                SpecialCaseBoxingTypes.Char,
                SpecialCaseBoxingTypes.SByte,
                SpecialCaseBoxingTypes.Byte,
                SpecialCaseBoxingTypes.Int16,
                SpecialCaseBoxingTypes.UInt16,
                SpecialCaseBoxingTypes.Int32,
                SpecialCaseBoxingTypes.UInt32,
                SpecialCaseBoxingTypes.Int64,
                SpecialCaseBoxingTypes.UInt64,
                SpecialCaseBoxingTypes.Single,
                SpecialCaseBoxingTypes.Double,
            ];
            MethodInfo method = typeof(StaticTestMethodSignatures).GetMethod(nameof(StaticTestMethodSignatures.Primitives));

            // Act
            ParameterBoxingInstructions[] actualInstructions = BoxingInstructions.GetBoxingInstructions(method);

            // Assert
            Assert.Equal(expectedInstructions, actualInstructions);
        }

        [Fact]
        public void GetBoxingInstructions_Handles_BuiltInReferenceTypes()
        {
            // Arrange
            ParameterBoxingInstructions[] expectedInstructions = [
                SpecialCaseBoxingTypes.Object,
                SpecialCaseBoxingTypes.Object,
                SpecialCaseBoxingTypes.Object,
            ];
            MethodInfo method = typeof(StaticTestMethodSignatures).GetMethod(nameof(StaticTestMethodSignatures.BuiltInReferenceTypes));

            // Act
            ParameterBoxingInstructions[] actualInstructions = BoxingInstructions.GetBoxingInstructions(method);

            // Assert
            Assert.Equal(expectedInstructions, actualInstructions);
        }
    }
}
