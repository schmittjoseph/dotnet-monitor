﻿// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using Microsoft.Diagnostics.Monitoring.HostingStartup.ParameterCapturing.FunctionProbes;
using Microsoft.Diagnostics.Monitoring.TestCommon;
using System.Reflection;
using Xunit;

namespace Microsoft.Diagnostics.Monitoring.HostingStartup.UnitTests.ParameterCapturing.FunctionProbes
{
    [TargetFrameworkMonikerTrait(TargetFrameworkMonikerExtensions.CurrentTargetFrameworkMoniker)]
    public class ProfilerAbiTests
    {
        [Fact]
        public void ParameterBoxingInstructions_IsBlittable()
        {
            EnsureIsBlittable<ParameterBoxingInstructions>();
        }

        private static void EnsureIsBlittable<T>() where T : unmanaged
        {
            // bool and char are both unmanaged types but are not blittable, check for them.
            FieldInfo[] fields = typeof(T).GetFields(BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Static | BindingFlags.Instance);

            foreach (FieldInfo field in fields)
            {
                if (field.FieldType == typeof(bool) ||
                    field.FieldType == typeof(char))
                {
                    Assert.Fail($"Field '{field.Name}' is not blittable");
                }
            }
        }
    }
    
}
