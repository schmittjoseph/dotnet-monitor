// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using Microsoft.Diagnostics.Monitoring.HostingStartup.ParameterCapturing;
using Microsoft.Diagnostics.Monitoring.StartupHook.MonitorMessageDispatcher.Models;
using Microsoft.Diagnostics.Monitoring.TestCommon;
using Xunit;

namespace Microsoft.Diagnostics.Monitoring.HostingStartup.UnitTests.ParameterCapturing
{
    [TargetFrameworkMonikerTrait(TargetFrameworkMonikerExtensions.CurrentTargetFrameworkMoniker)]
    public class TypeUtilsTests
    {
        [Theory]
        [InlineData("ClassInNamespace", "ClassInNamespace.MyClass", true)]
        [InlineData("NestedType", "NestedType+MyNestedType", true)]
        [InlineData("SameAsNamespace", "SameAsNamespace", true)]
        [InlineData("DifferentCasing", "differentcasing", false)]
        [InlineData("CustomNamespace.Microsoft", "Microsoft", false)]
        [InlineData("SubString2", "SubString", false)]
        [InlineData("SubString", "SubString2", false)]
        public void IsSubType(string parentType, string typeName, bool isSubType)
        {
            Assert.Equal(isSubType, TypeUtils.IsSubType(parentType, typeName));
        }

        [Theory]
        // No generics
        [InlineData("System.String", "System.String")]
        // Generic instantiation
        [InlineData("List`1[System.String]", "List`1")]
        [InlineData("Dictionary`2[Int32,List`1[String]]", "Dictionary`2")]
        [InlineData("GenericBaseClass`1[System.String].GenericNestedClass`1[System.String]", "GenericBaseClass`1.GenericNestedClass`1")]
        // Generic parameters in a generic method definition
        [InlineData("DoWork[System.System]", "DoWork")]
        // Malformed
        [InlineData("List`1[System.String", null)]
        [InlineData("List`1]", null)]
        [InlineData("List`1][", null)]
        // Input validation
        [InlineData("", "")]
        public void TryStripGenerics_SingleName(string name, string expectedResult)
        {
            bool didSucceed = TypeUtils.TryStripGenerics(name, out string actualResult);
            if (expectedResult == null)
            {
                Assert.False(didSucceed);
                return;
            }

            Assert.True(didSucceed);
            Assert.Equal(expectedResult, actualResult);
        }

        [Fact]
        public void TryStripGenerics_MethodDescription_Rejects_MalformedType()
        {
            // Arrange
            MethodDescription methodDescription = new()
            {
                ModuleName = "MyModule.dll",
                TypeName = "A[",
                MethodName = "MyMethod",
            };

            // Act
            bool didSucceed = TypeUtils.TryStripGenerics(methodDescription, out _);

            // Assert
            Assert.False(didSucceed);
        }

        [Fact]
        public void TryStripGenerics_MethodDescription_Rejects_MalformedMethod()
        {
            // Arrange
            MethodDescription methodDescription = new()
            {
                ModuleName = "MyModule.dll",
                TypeName = "MyType",
                MethodName = "MyMethod[",
            };

            // Act
            bool didSucceed = TypeUtils.TryStripGenerics(methodDescription, out _);

            // Assert
            Assert.False(didSucceed);
        }

        [Fact]
        public void TryStripGenerics_MethodDescription_DoesStrip()
        {
            // Arrange
            const string BaseTypeName = "MyType`1";
            const string BaseMethodName = "MyMethod`1";

            MethodDescription methodDescription = new()
            {
                ModuleName = "MyModule.dll",
                TypeName = $"{BaseTypeName}[String]",
                MethodName = $"{BaseMethodName}[String]",
            };

            // Act
            bool didSucceed = TypeUtils.TryStripGenerics(methodDescription, out MethodDescription result);

            // Assert
            Assert.True(didSucceed);
            Assert.Equal(methodDescription.ModuleName, result.ModuleName);
            Assert.Equal(BaseTypeName, result.TypeName);
            Assert.Equal(BaseMethodName, result.MethodName);
        }

        [Fact]
        public void TryStripGenerics_MethodDescription_DoesNotStripModuleName()
        {
            // Arrange

            MethodDescription methodDescription = new()
            {
                ModuleName = "MyModule[System.String].dll",
                TypeName = "MyType",
                MethodName = "MyMethod",
            };

            // Act
            bool didSucceed = TypeUtils.TryStripGenerics(methodDescription, out MethodDescription result);

            // Assert
            Assert.True(didSucceed);
            Assert.Equal(methodDescription.ModuleName, result.ModuleName);
        }
    }
}
