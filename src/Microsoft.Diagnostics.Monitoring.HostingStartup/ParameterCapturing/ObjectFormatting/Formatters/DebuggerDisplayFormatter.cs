// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using System;
using System.Collections.Generic;
using System.Reflection;
using System.Text;

namespace Microsoft.Diagnostics.Monitoring.HostingStartup.ParameterCapturing.ObjectFormatting.Formatters
{
    internal static class DebuggerDisplayFormatter
    {
        internal record DebuggerDisplayAttributeValue(string Value, IEnumerable<Type> EncompassingTypes);
        internal delegate object? BoundEvaluator(object instanceObj);
        internal record ExpressionEvaluator(BoundEvaluator Evaluator, Type ReturnType);
        internal record Expression(ReadOnlyMemory<char> ExpressionString, FormatSpecifier FormatSpecifier);

        internal record ParsedDebuggerDisplay(string FormatString, Expression[] Expressions);

        public static FormatterFactoryResult? GetDebuggerDisplayFormatter(Type? objType)
        {
            if (objType == null || objType.IsInterface)
            {
                return null;
            }

            DebuggerDisplayAttributeValue? attribute = GetDebuggerDisplayAttribute(objType);
            if (attribute == null)
            {
                return null;
            }

            ParsedDebuggerDisplay? parsedDebuggerDiplay = ParseDebuggerDisplay(attribute.Value.AsMemory());
            if (parsedDebuggerDiplay == null)
            {
                return null;
            }

            ObjectFormatterFunc? formatter = BindParsedDebuggerDisplay(objType, parsedDebuggerDiplay);
            if (formatter == null)
            {
                return null;
            }

            return new FormatterFactoryResult(formatter, attribute.EncompassingTypes);
        }

        internal static ObjectFormatterFunc? BindParsedDebuggerDisplay(Type objType, ParsedDebuggerDisplay debuggerDisplay)
        {
            if (debuggerDisplay.Expressions.Length == 0)
            {
                return (_, _) => debuggerDisplay.FormatString;
            }

            ExpressionEvaluator[] boundExpressions = new ExpressionEvaluator[debuggerDisplay.Expressions.Length];
            ObjectFormatterFunc[] evaluatorFormatters = new ObjectFormatterFunc[debuggerDisplay.Expressions.Length];

            for (int i = 0; i < debuggerDisplay.Expressions.Length; i++)
            {
                ExpressionEvaluator? evaluator = BindExpression(objType, debuggerDisplay.Expressions[i].ExpressionString.Span);
                if (evaluator == null)
                {
                    return null;
                }

                boundExpressions[i] = evaluator;
                evaluatorFormatters[i] = ObjectFormatterFactory.GetFormatter(evaluator.ReturnType, useDebuggerDisplayAttribute: false).Formatter;
            }

            return (obj, formatSpecifier) =>
            {
                string[] evaluationResults = new string[boundExpressions.Length];
                for (int i = 0; i < boundExpressions.Length; i++)
                {
                    object? evaluationResult = boundExpressions[i].Evaluator(obj);
                    if (evaluationResult == null)
                    {
                        evaluationResults[i] = ObjectFormatter.Tokens.Null;
                        continue;
                    }

                    evaluationResults[i] = ObjectFormatter.FormatObject(
                        evaluatorFormatters[i],
                        evaluationResult,
                        debuggerDisplay.Expressions[i].FormatSpecifier);
                }

                return string.Format(debuggerDisplay.FormatString, evaluationResults);
            };
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

        internal static DebuggerDisplayAttributeValue? GetDebuggerDisplayAttribute(Type objType)
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

                        return new DebuggerDisplayAttributeValue(value, encompassingTypes);
                    }
                }

                currentType = currentType.BaseType;
            }

            return null;
        }

        internal static ParsedDebuggerDisplay? ParseDebuggerDisplay(ReadOnlyMemory<char> expression)
        {
            StringBuilder fmtString = new();
            List<Expression> subExpressions = new();

            ReadOnlySpan<char> expressionSpan = expression.Span;

            for (int i = 0; i < expressionSpan.Length; i++)
            {
                char c = expressionSpan[i];
                switch (c)
                {
                    case '{':
                        Expression? parsedExpression = ParseExpression(expression.Slice(i), out int charsRead);
                        if (parsedExpression == null)
                        {
                            return null;
                        }
                        i += charsRead;

                        fmtString.Append('{');
                        fmtString.Append(subExpressions.Count);
                        fmtString.Append('}');

                        subExpressions.Add(parsedExpression);

                        break;
                    case '}':
                        // Malformed
                        return null;

                    default:
                        fmtString.Append(c);
                        break;
                }
            }

            return new ParsedDebuggerDisplay(fmtString.ToString(), subExpressions.ToArray());
        }

        internal static Expression? ParseExpression(ReadOnlyMemory<char> expression, out int length)
        {
            length = 0;
            if (expression.Length == 0)
            {
                return null;
            }

            ReadOnlySpan<char> spanExpression = expression.Span;

            if (spanExpression[0] != '{')
            {
                return null;
            }

            int parenthesisDepth = 0;
            int formatSpecifiersStart = -1;

            // Find the format specifier (if any) and split on that.
            for (int i = 1; i < expression.Length; i++)
            {
                length++;
                char c = spanExpression[i];
                switch (c)
                {
                    case '(':
                        parenthesisDepth++;
                        break;

                    case ')':
                        if (parenthesisDepth-- < 0)
                        {
                            return null;
                        }
                        break;

                    case '{':
                        return null;

                    case '}':
                        // End of expression or malformed
                        if (parenthesisDepth != 0)
                        {
                            return null;
                        }

                        if (formatSpecifiersStart != -1)
                        {
                            return new Expression(
                                expression.Slice(1, formatSpecifiersStart - 1),
                                ConvertFormatSpecifier(spanExpression.Slice(formatSpecifiersStart, i - formatSpecifiersStart)));
                        }

                        return new Expression(
                            expression.Slice(1, length - 1),
                            FormatSpecifier.None);

                    case ',':
                        if (parenthesisDepth == 0 && formatSpecifiersStart == -1)
                        {
                            formatSpecifiersStart = i;
                        }
                        break;
                    default:
                        break;

                }
            }

            return null;

        }

        internal static FormatSpecifier ConvertFormatSpecifier(ReadOnlySpan<char> specifiers)
        {
            FormatSpecifier formatSpecifier = FormatSpecifier.None;

            void parseSpecifier(ReadOnlySpan<char> specifier)
            {
                if (specifier.Length == 0)
                {
                    return;
                }

                if (specifier.Equals("nq", StringComparison.Ordinal))
                {
                    formatSpecifier |= FormatSpecifier.NoQuotes;
                }
            }

            int startIndex = 0;
            int length = 0;
            for (int i = 0; i < specifiers.Length; i++)
            {
                char c = specifiers[i];

                if (c == ',')
                {
                    parseSpecifier(specifiers.Slice(startIndex, length));
                    startIndex = i + 1;
                    length = 0;
                    continue;
                }

                length++;
            }

            parseSpecifier(specifiers.Slice(startIndex, length));
            return formatSpecifier;
        }
    }
}
