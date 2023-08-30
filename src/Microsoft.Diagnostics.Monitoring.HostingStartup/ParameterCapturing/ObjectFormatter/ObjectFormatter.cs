// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.Reflection;
using System.Text;

namespace Microsoft.Diagnostics.Monitoring.HostingStartup.ParameterCapturing.ObjectFormatter
{
    internal sealed class ObjectFormatter
    {
        public delegate string Formatter(object obj);


        private delegate object? BoundEvaluator(object instanceObj);
        private record ExpressionEvaluator(BoundEvaluator Evaluator, Type ReturnType);
        private record Expression(string ExpressionString, FormatSpecifier FormatSpecifier);

        // ref: https://learn.microsoft.com/visualstudio/debugger/format-specifiers-in-csharp#format-specifiers
        [Flags] private enum FormatSpecifier
        {
            None = 0,
            NoQuotes = 1
        }

        private record ParsedDebuggerDisplay(string FormatString, Expression[] Expressions);
        private readonly ConcurrentDictionary<Type, Formatter> _formatters = new();

        private static string IConvertibleFormatter(object obj) => ((IConvertible)obj).ToString(CultureInfo.InvariantCulture);
        private static string IFormattableFormatter(object obj) => ((IFormattable)obj).ToString(format: null, CultureInfo.InvariantCulture);
        private static string GeneralFormatter(object obj) => string.Concat(
            PrettyPrinter.Tokens.Parameters.Values.WrappedStart,
            obj.ToString() ?? string.Empty,
            PrettyPrinter.Tokens.Parameters.Values.WrappedEnd);

        public Formatter GetFormatter(Type objType, bool useDebuggerDisplayAttribute)
        {
            if (_formatters.TryGetValue(objType, out Formatter? formatter) && formatter != null)
            {
                return formatter;
            }

            if (useDebuggerDisplayAttribute)
            {
                formatter = GetDebuggerDisplayFormatter(objType);
            }

            if (formatter == null)
            {
                if (objType is IConvertible)
                {
                    formatter = IConvertibleFormatter;
                }
                else if (objType is IFormattable)
                {
                    formatter = IFormattableFormatter;
                }
                else
                {
                    formatter = GeneralFormatter;
                }
            }

            _ = _formatters.TryAdd(objType, formatter);
            return formatter;
        }

        private static Formatter? GetDebuggerDisplayFormatter(Type? objType)
        {
            if (objType == null)
            {
                return null;
            }

            string? debuggerDisplayValue = GetDebuggerDisplayAttributeValue(objType);
            if (debuggerDisplayValue == null)
            {
                return null;
            }

            ParsedDebuggerDisplay? parsedDebuggerDiplay = ParseDebuggerDisplay(debuggerDisplayValue);
            if (parsedDebuggerDiplay == null)
            {
                return null;
            }

            return BindParsedDebuggerDisplay(objType, parsedDebuggerDiplay);
        }

        private static Formatter? BindParsedDebuggerDisplay(Type objType, ParsedDebuggerDisplay debuggerDisplay)
        {
            if (debuggerDisplay.Expressions.Length == 0)
            {
                return (_) => debuggerDisplay.FormatString;
            }

            ExpressionEvaluator[] boundExpressions = new ExpressionEvaluator[debuggerDisplay.Expressions.Length];
            for (int i = 0; i < debuggerDisplay.Expressions.Length; i++)
            {
                ExpressionEvaluator? evaluator = BindExpression(objType, debuggerDisplay.Expressions[i].ExpressionString);
                if (evaluator == null)
                {
                    return null;
                }

                boundExpressions[i] = evaluator;
            }

            return (obj) =>
            {
                string[] evaluationResults = new string[boundExpressions.Length];
                for (int i = 0; i < boundExpressions.Length; i++)
                {
                    string? res = boundExpressions[i].Evaluator(obj)?.ToString();
                    if (res == null)
                    {
                        evaluationResults[i] = PrettyPrinter.Tokens.Parameters.Values.Null;
                    }
                    else
                    {
                        // JSFIX: IConvertible & etc.
                        FormatSpecifier formatSpecificer = debuggerDisplay.Expressions[i].FormatSpecifier;

                        if ((formatSpecificer & FormatSpecifier.NoQuotes) != 0)
                        {
                            res = string.Concat(PrettyPrinter.Tokens.Parameters.Values.WrappedStart, res, PrettyPrinter.Tokens.Parameters.Values.WrappedEnd);
                        }

                        evaluationResults[i] = res;
                    }
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

        private static string? GetDebuggerDisplayAttributeValue(Type? objType)
        {
            if (objType == null)
            {
                return null;
            }

            // IMemoryCache
            // - use the one in Microsoft.Extensions

            foreach (CustomAttributeData attr in objType.CustomAttributes)
            {
                if (attr.AttributeType == typeof(DebuggerDisplayAttribute) && attr.ConstructorArguments.Count > 0)
                {
                    string? value = attr.ConstructorArguments[0].Value?.ToString();
                    if (value == null)
                    {
                        continue;
                    }

                    return value;
                }
            }

            return GetDebuggerDisplayAttributeValue(objType.BaseType);
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

        public void ClearCache()
        {
            _formatters.Clear();
        }
    }
}
