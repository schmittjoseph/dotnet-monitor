// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using Microsoft.Diagnostics.Monitoring.HostingStartup.ParameterCapturing;
using Microsoft.Diagnostics.Monitoring.TestCommon;
using System;
using System.Collections;
using System.Collections.Generic;
using System.Reflection;
using Xunit;

namespace Microsoft.Diagnostics.Monitoring.HostingStartup.UnitTests.ParameterCapturing
{
    [TargetFrameworkMonikerTrait(TargetFrameworkMonikerExtensions.CurrentTargetFrameworkMoniker)]
    public class BoxingTokensTests
    {
        [Theory]
        [InlineData(typeof(TestMethodSignatures), nameof(TestMethodSignatures.ImplicitThis), true)]
        [InlineData(typeof(TestMethodSignatures), nameof(TestMethodSignatures.RefStruct), true, false)]
        [InlineData(typeof(StaticTestMethodSignatures), nameof(StaticTestMethodSignatures.NoArgs))]
        [InlineData(typeof(StaticTestMethodSignatures), nameof(StaticTestMethodSignatures.ExplicitThis), true)]
        [InlineData(typeof(StaticTestMethodSignatures), nameof(StaticTestMethodSignatures.RefParam), false)]
        [InlineData(typeof(StaticTestMethodSignatures), nameof(StaticTestMethodSignatures.OutParam), false)]
        [InlineData(typeof(StaticTestMethodSignatures), nameof(StaticTestMethodSignatures.TypeDef), true)]
        [InlineData(typeof(StaticTestMethodSignatures), nameof(StaticTestMethodSignatures.TypeRef), true)]
        [InlineData(typeof(StaticTestMethodSignatures), nameof(StaticTestMethodSignatures.TypeSpec), true)]
        [InlineData(typeof(StaticTestMethodSignatures), nameof(StaticTestMethodSignatures.TypeDefValueType), true)]
        [InlineData(typeof(StaticTestMethodSignatures), nameof(StaticTestMethodSignatures.TypeRefValueType), false)]
        [InlineData(typeof(StaticTestMethodSignatures), nameof(StaticTestMethodSignatures.TypeSpecValueType), false)]
        public void GetBoxingTokens_HandlesUnsupportedArgs(Type t, string name, params bool[] supported)
        {
            // Arrange
            MethodInfo method = t.GetMethod(name);

            // Act
            uint[] tokens = BoxingTokens.GetBoxingTokens(method);
            bool[] supportedArgs = BoxingTokens.GetSupportedArgs(tokens);

            // Assert
            Assert.Equal(supported, supportedArgs);
        }
    }

    internal struct MyTestStruct { }
    internal ref struct MyRefStruct { }
    internal enum MyEnum { }

#pragma warning disable CA1822 // Mark members as static
    internal sealed class TestMethodSignatures
    {
        public void ImplicitThis()
        {

        }

        public void RefStruct(ref MyRefStruct myRefStruct)
        {
        }
    }
#pragma warning restore CA1822 // Mark members as static

    internal static class StaticTestMethodSignatures
    {
        public static void NoArgs()
        {

        }
        public static void ExplicitThis(this object thisObj)
        {

        }

        public static void RefParam(ref int i)
        {
        }

        public static void OutParam(out int i)
        {
            i = 0;
        }

        public static void TypeRef(Uri uri)
        {

        }

        public static void TypeDef(TestMethodSignatures t)
        {

        }

        public static void TypeSpec(IList<IEnumerable<bool>> list)
        {

        }


        public static void TypeDefValueType(MyEnum myEnum)
        {

        }

        public static void TypeRefValueType(TypeCode typeCode)
        {

        }

        public static void TypeSpecValueType(bool? b)
        {

        }
    }

}
