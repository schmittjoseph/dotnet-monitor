// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using Microsoft.Diagnostics.Monitoring.HostingStartup.ParameterCapturing.ObjectFormatter;
using Microsoft.Diagnostics.Monitoring.TestCommon;
using SampleMethods;
using System;
using System.Diagnostics;
using System.Linq;
using Xunit;
using Xunit.Abstractions;
using static Microsoft.Diagnostics.Monitoring.HostingStartup.ParameterCapturing.ObjectFormatter.DebuggerDisplay;

namespace Microsoft.Diagnostics.Monitoring.HostingStartup.UnitTests.ParameterCapturing
{
    [TargetFrameworkMonikerTrait(TargetFrameworkMonikerExtensions.CurrentTargetFrameworkMoniker)]
    public class DebuggerDisplayTests
    {
        private readonly ITestOutputHelper _outputHelper;

        public DebuggerDisplayTests(ITestOutputHelper outputHelper)
        {
            _outputHelper = outputHelper;
        }

        // Test chained evaluations with implicit-this-type switching mid-chain.
        [System.Diagnostics.DebuggerDisplay("test")]
        private class DebuggerDisplayClass
        {
            public int Count { get; set; } = 10;

            public Uri MyUri { get; }

            public DebuggerDisplayClass(string uri)
            {
                RecursionProp = this;
                MyUri = new Uri(uri);
            }

            public DebuggerDisplayClass RecursionProp { get; }
            public DebuggerDisplayClass Recursion() => this;

            public static void WithArgs(int i) { }

            public string GetCountAsString()
            {
                return Count.ToString();
            }

            public void NoReturnType() => Count++;
        }

        private sealed class DerivedWithBaseDebuggerDisplay : DebuggerDisplayClass
        {
            public DerivedWithBaseDebuggerDisplay(string uri) : base(uri) { }
        }

        private sealed class NoDebuggerDisplay { }

        [Theory]
        [InlineData(typeof(NoDebuggerDisplay), null)]
        [InlineData(typeof(DebuggerDisplayClass), "test")]
        [InlineData(typeof(DerivedWithBaseDebuggerDisplay), "test")]
        public void GetDebuggerDisplayAttribute(Type type, string expected)
        {
            // Act
            string actual = DebuggerDisplay.GetDebuggerDisplayAttribute(type)?.Value;

            // Assert
            Assert.Equal(expected, actual);
        }

        [Theory]
        [InlineData(typeof(NoDebuggerDisplay), null)]
        [InlineData(typeof(DebuggerDisplayClass), 1)]
        [InlineData(typeof(DerivedWithBaseDebuggerDisplay), 2)]
        public void GetDebuggerDisplayAttribute_EncompassingTypes(Type type, int? expectedEncompassedTypes)
        {
            // Act
            int? actual = DebuggerDisplay.GetDebuggerDisplayAttribute(type)?.EncompassingTypes?.Count();

            // Assert
            Assert.Equal(expectedEncompassedTypes, actual);
        }

        [Theory]
        // No expressions
        [InlineData("no_expressions", "no_expressions")]
        // Balanced expressions
        [InlineData("{valid} }invalid_expression", null)]
        [InlineData("{valid} {invalid_expression", null)]
        [InlineData("{{invalid_expression}}", null)]
        // Method expressions
        [InlineData("Test: {methodName()}", "Test: {0}", "methodName()")]
        [InlineData("Test: {methodName(ArgName, SecondArg)}", "methodName(ArgName, SecondArg)")]
        // Property expressions
        [InlineData("Test: {propertyName}", "Test: {0}", "propertyName")]
        // Chained expressions
        [InlineData("Test: {methodName().propertyName.methodName()}", "Test: {0}", "methodName().propertyName.methodName()")]
        // Format specificers
        [InlineData("Test: {methodName(ArgName, SecondArg),raw,nq}", "Test: {0}", "methodName(ArgName, SecondArg)")]
        // Multiple expressions
        [InlineData("Test: {prop1} - {prop2} - {method()}", "Test: {0} - {1} - {2}", "prop1", "prop2", "method()")]
        // Complex expressions
        [InlineData("Test: {propertyName - 2}", "Test: {0}", "propertyName - 2")]
        public void ParseDebuggerDisplay(string debuggerDisplay, string formatString, params string[] expressions)
        {
            // Act
            ParsedDebuggerDisplay parsed = DebuggerDisplay.ParseDebuggerDisplay(debuggerDisplay);

            // Assert
            if (formatString == null)
            {
                Assert.Null(parsed);
                return;
            }

            Assert.NotNull(parsed);
            Assert.Equal(parsed.FormatString, formatString);
            Assert.Equal(parsed.Expressions.Select(p => p.ExpressionString), expressions);
        }

        [Theory]
        [InlineData("GetCountAsString()", true, "10")]
        [InlineData("DoesntExist()", false, null)]
        [InlineData("WithArgs(Count)", false, null)]
        [InlineData("Count", true, 10)]
        [InlineData("NoReturnType()", true, null)]
        // Chained expression with implicit this type change
        [InlineData("Recursion().RecursionProp.MyUri.Host", true, "www.bing.com")]
        public void BindExpression(string expression, bool doesBind, object expected)
        {
            // Arrange
            DebuggerDisplayClass obj = new("https://www.bing.com/abc");

            // Act
            ExpressionEvaluator evaluator = DebuggerDisplay.BindExpression(obj.GetType(), expression);
            object result = evaluator?.Evaluator(obj);

            // Assert
            if (!doesBind)
            {
                Assert.Null(evaluator);
                return;
            }

            Assert.NotNull(evaluator);
            Assert.Equal(expected, result);
        }

        [Theory]
        [InlineData(FormatSpecifier.None)]
        [InlineData(FormatSpecifier.NoQuotes, "nq")]
        [InlineData(FormatSpecifier.None, "NQ")]
        [InlineData(FormatSpecifier.NoQuotes, "nq", "raw")]
        // JSFIX: Visibility

        internal void ConvertFormatSpecifier(FormatSpecifier expected, params string[] specifiers)
        {
            Assert.Equal(expected, DebuggerDisplay.ConvertFormatSpecifier(specifiers));
        }
    }
}
