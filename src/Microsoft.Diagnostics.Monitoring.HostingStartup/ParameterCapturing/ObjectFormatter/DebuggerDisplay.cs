// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using System;
using System.Collections.Generic;
using System.Reflection;
using System.Text;

namespace Microsoft.Diagnostics.Monitoring.HostingStartup.ParameterCapturing.ObjectFormatter
{
    internal static class DebuggerDisplay
    {
        private delegate object? BoundEvaluator(object instanceObj);

        private record DebuggerDisplayAttribute(string Value, IEnumerable<Type> EncompassingTypes);
        private record ExpressionEvaluator(BoundEvaluator Evaluator, Type ReturnType);
        private record Expression(string ExpressionString, FormatSpecifier FormatSpecifier);

        // ref: https://learn.microsoft.com/visualstudio/debugger/format-specifiers-in-csharp#format-specifiers
        [Flags] private enum FormatSpecifier
        {
            None = 0,
            NoQuotes = 1
        }

        private record ParsedDebuggerDisplay(string FormatString, Expression[] Expressions);

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

        private static ObjectFormatter.Formatter? BindParsedDebuggerDisplay(Type objType, ParsedDebuggerDisplay debuggerDisplay)
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

                    string formattedEvaluationResult = evaluatorFormatters[i](evaluationResult);
                    FormatSpecifier formatSpecificer = debuggerDisplay.Expressions[i].FormatSpecifier;

                    if ((formatSpecificer & FormatSpecifier.NoQuotes) != 0)
                    {
                        formattedEvaluationResult = string.Concat(
                            MethodTemplateStringGenerator.Tokens.Parameters.Values.WrappedStart,
                            formattedEvaluationResult,
                            MethodTemplateStringGenerator.Tokens.Parameters.Values.WrappedEnd);
                    }

                    evaluationResults[i] = formattedEvaluationResult;
                }

                return string.Format(debuggerDisplay.FormatString, evaluationResults);
            };
        }

        private static ExpressionEvaluator? BindExpression(Type objType, ReadOnlySpan<char> expression)
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

        private static DebuggerDisplayAttribute? GetDebuggerDisplayAttribute(Type objType)
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

        static ParsedDebuggerDisplay? ParseDebuggerDisplay(string expression)
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
            foreach (char c in expression)
            {
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

        private static FormatSpecifier ConvertFormatSpecifier(IEnumerable<string> specifiers)
        {
            FormatSpecifier formatSpecifier = FormatSpecifier.None;

            foreach (var specifier in specifiers)
            {
                if (string.Equals(specifier, "nq", StringComparison.Ordinal))
                {
                    formatSpecifier &= FormatSpecifier.NoQuotes;
                }
            }

            return formatSpecifier;
        }
    }
}
