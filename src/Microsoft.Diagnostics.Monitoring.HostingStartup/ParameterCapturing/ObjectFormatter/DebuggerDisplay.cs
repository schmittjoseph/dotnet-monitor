// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using System;
using System.Collections.Generic;
using System.Reflection;
using System.Text;
using static Microsoft.Diagnostics.Monitoring.HostingStartup.ParameterCapturing.ObjectFormatter.DebuggerDisplay;

namespace Microsoft.Diagnostics.Monitoring.HostingStartup.ParameterCapturing.ObjectFormatter
{
    internal static class DebuggerDisplay
    {
        internal delegate object? BoundEvaluator(object instanceObj);

        internal record DebuggerDisplayAttribute(string Value, IEnumerable<Type> EncompassingTypes);
        internal record ExpressionEvaluator(BoundEvaluator Evaluator, Type ReturnType);
        internal record Expression(string ExpressionString, FormatSpecifier FormatSpecifier);

        // ref: https://learn.microsoft.com/visualstudio/debugger/format-specifiers-in-csharp#format-specifiers
        [Flags]
        public enum FormatSpecifier
        {
            None = 0,
            NoQuotes = 1
        }

        internal record ParsedDebuggerDisplay(string FormatString, Expression[] Expressions);

        public static ObjectFormatter.GeneratedFormatter? GetDebuggerDisplayFormatter(Type? objType)
        {
            if (objType == null || objType.IsInterface)
            {
                return null;
            }

            DebuggerDisplayAttribute? attribute = GetDebuggerDisplayAttribute(objType);
            if (attribute == null)
            {
                return null;
            }

            ParsedDebuggerDisplay? parsedDebuggerDiplay = ParseDebuggerDisplay(attribute.Value);
            if (parsedDebuggerDiplay == null)
            {
                return null;
            }

            ObjectFormatter.Formatter? formatter = BindParsedDebuggerDisplay(objType, parsedDebuggerDiplay);
            if (formatter == null)
            {
                return null;
            }

            return new ObjectFormatter.GeneratedFormatter(formatter, attribute.EncompassingTypes);
        }

        internal static ObjectFormatter.Formatter? BindParsedDebuggerDisplay(Type objType, ParsedDebuggerDisplay debuggerDisplay)
        {
            if (debuggerDisplay.Expressions.Length == 0)
            {
                return (_) => debuggerDisplay.FormatString;
            }

            ExpressionEvaluator[] boundExpressions = new ExpressionEvaluator[debuggerDisplay.Expressions.Length];
            ObjectFormatter.Formatter[] evaluatorFormatters = new ObjectFormatter.Formatter[debuggerDisplay.Expressions.Length];

            for (int i = 0; i < debuggerDisplay.Expressions.Length; i++)
            {
                ExpressionEvaluator? evaluator = BindExpression(objType, debuggerDisplay.Expressions[i].ExpressionString);
                if (evaluator == null)
                {
                    return null;
                }

                boundExpressions[i] = evaluator;
                evaluatorFormatters[i] = ObjectFormatter.GetFormatter(evaluator.ReturnType, useDebuggerDisplayAttribute: false).Formatter;
            }

            return (obj) =>
            {
                string[] evaluationResults = new string[boundExpressions.Length];
                for (int i = 0; i < boundExpressions.Length; i++)
                {
                    object? evaluationResult = boundExpressions[i].Evaluator(obj);
                    if (evaluationResult == null)
                    {
                        evaluationResults[i] = MethodTemplateStringGenerator.Tokens.Parameters.Values.Null;
                        continue;
                    }

                    evaluationResults[i] = ApplyFormatSpecifiers(
                        evaluatorFormatters[i](evaluationResult),
                        debuggerDisplay.Expressions[i].FormatSpecifier);
                }

                return string.Format(debuggerDisplay.FormatString, evaluationResults);
            };
        }

        internal static string ApplyFormatSpecifiers(string result, FormatSpecifier formatSpecifier)
        {
            if ((formatSpecifier & FormatSpecifier.NoQuotes) != 0)
            {
                result = string.Concat(
                    MethodTemplateStringGenerator.Tokens.Parameters.Values.WrappedStart,
                    result,
                    MethodTemplateStringGenerator.Tokens.Parameters.Values.WrappedEnd);
            }

            return result;
        }

        internal static ExpressionEvaluator? BindExpression(Type objType, ReadOnlySpan<char> expression)
        {
            int nestedObjIndex = expression.IndexOf('.');
            if (nestedObjIndex != -1)
            {
                ExpressionEvaluator? topLevelExpression = BindExpression(objType, expression[..nestedObjIndex]);
                if (topLevelExpression == null)
                {
                    return null;
                }

                ExpressionEvaluator? nestedExpression = BindExpression(topLevelExpression.ReturnType, expression[(nestedObjIndex + 1)..]);
                if (nestedExpression == null)
                {
                    return null;
                }

                return new ExpressionEvaluator(
                    (obj) =>
                    {
                        object? topLevelInstanceObj = topLevelExpression.Evaluator(obj);
                        if (topLevelInstanceObj == null)
                        {
                            return null;
                        }

                        return nestedExpression.Evaluator(topLevelInstanceObj);
                    },
                    nestedExpression.ReturnType);
            }

            if (expression.EndsWith("()"))
            {
                // TODO: Ambingious
                MethodInfo? method = objType.GetMethod(
                    expression[..^2].ToString(),
                    BindingFlags.NonPublic | BindingFlags.Public | BindingFlags.Instance | BindingFlags.Static,
                    Array.Empty<Type>());

                if (method == null)
                {
                    return null;
                }

                return new ExpressionEvaluator(
                    (obj) => method.Invoke(obj, parameters: null),
                    method.ReturnType);
            }
            else
            {
                PropertyInfo? property = objType.GetProperty(expression.ToString());
                if (property == null)
                {
                    return null;
                }

                return new ExpressionEvaluator(property.GetValue, property.PropertyType);
            }
        }

        internal static DebuggerDisplayAttribute? GetDebuggerDisplayAttribute(Type objType)
        {
            List<Type> encompassingTypes = new();

            Type? currentType = objType;
            while (currentType != null && currentType != typeof(object))
            {
                encompassingTypes.Add(currentType);

                foreach (CustomAttributeData attr in currentType.CustomAttributes)
                {
                    if (attr.AttributeType == typeof(System.Diagnostics.DebuggerDisplayAttribute))
                    {
                        string? value = attr.ConstructorArguments[0].Value?.ToString();
                        if (value == null)
                        {
                            continue;
                        }

                        return new DebuggerDisplayAttribute(value, encompassingTypes);
                    }
                }

                currentType = currentType.BaseType;
            }

            return null;
        }

        internal static ParsedDebuggerDisplay? ParseDebuggerDisplay(string expression)
        {
            StringBuilder fmtString = new();
            List<Expression> subExpressions = new();

            StringBuilder expressionBuilder = new();
            List<string> formatSpecifiers = new();
            StringBuilder formatSpecifierBuilder = new();

            bool inFormatSpecifier = false;

            void stageFormatSpecifier()
            {
                inFormatSpecifier = false;
                if (formatSpecifierBuilder.Length != 0)
                {
                    formatSpecifiers.Add(formatSpecifierBuilder.ToString());
                    formatSpecifierBuilder.Clear();
                }
            }

            void stageExpression()
            {
                stageFormatSpecifier();

                subExpressions.Add(new Expression(expressionBuilder.ToString(), ConvertFormatSpecifier(formatSpecifiers)));
                formatSpecifiers.Clear();
            }

            void startExpression()
            {
                expressionBuilder.Clear();
                fmtString.Append('{');
                fmtString.Append(subExpressions.Count);
                fmtString.Append('}');
            }

            int expressionDepth = 0;
            for (int i = 0; i < expression.Length; i++)
            {
                char c = expression[i];
                switch (c)
                {
                    case '}':
                        expressionDepth--;
                        if (expressionDepth < 0)
                        {
                            return null;
                        }

                        stageExpression();

                        break;

                    case '{':
                        expressionDepth++;
                        if (expressionDepth > 1)
                        {
                            return null;
                        }

                        startExpression();

                        break;
                    case ',':
                        if (expressionDepth == 0)
                        {
                            goto default;
                        }

                        stageFormatSpecifier();
                        break;
                    default:
                        StringBuilder strBuilder;
                        if (expressionDepth == 0)
                        {
                            strBuilder = fmtString;
                        }
                        else if (inFormatSpecifier)
                        {
                            strBuilder = formatSpecifierBuilder;
                        }
                        else
                        {
                            strBuilder = expressionBuilder;
                        }

                        strBuilder.Append(c);
                        break;
                }
            }

            return expressionDepth == 0
                ? new ParsedDebuggerDisplay(fmtString.ToString(), subExpressions.ToArray())
                : null;
        }

        internal static FormatSpecifier ConvertFormatSpecifier(IEnumerable<string> specifiers)
        {
            FormatSpecifier formatSpecifier = FormatSpecifier.None;

            foreach (var specifier in specifiers)
            {
                if (string.Equals(specifier, "nq", StringComparison.Ordinal))
                {
                    formatSpecifier |= FormatSpecifier.NoQuotes;
                }
            }

            return formatSpecifier;
        }
    }
}
