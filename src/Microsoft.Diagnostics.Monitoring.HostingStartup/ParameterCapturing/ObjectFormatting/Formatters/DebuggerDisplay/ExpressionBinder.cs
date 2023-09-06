// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using System;
using System.Collections.Generic;
using System.Reflection;
using static Microsoft.Diagnostics.Monitoring.HostingStartup.ParameterCapturing.ObjectFormatting.Formatters.DebuggerDisplay.DebuggerDisplayParser;

namespace Microsoft.Diagnostics.Monitoring.HostingStartup.ParameterCapturing.ObjectFormatting.Formatters.DebuggerDisplay
{
    internal static class ExpressionBinder
    {
        internal delegate object? BoundEvaluator(object instanceObj);
        internal record ExpressionEvaluator(BoundEvaluator Evaluator, Type ReturnType);

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
            ReadOnlySpan<char> unboundExpression = expression;
            List<ExpressionEvaluator> evaluatorChain = new();

            Type chainedContextType = objType;
            while (!unboundExpression.IsEmpty)
            {
                int chainedExpressionIndex = unboundExpression.IndexOf('.');
                ExpressionEvaluator? evaluator;
                if (chainedExpressionIndex != -1)
                {
                    evaluator = BindSingleExpression(unboundExpression[..chainedExpressionIndex], chainedContextType);
                    unboundExpression = unboundExpression[(chainedExpressionIndex + 1)..];
                }
                else
                {
                    evaluator = BindSingleExpression(unboundExpression, chainedContextType);
                    unboundExpression = ReadOnlySpan<char>.Empty;
                }

                if (evaluator == null)
                {
                    return null;
                }

                evaluatorChain.Add(evaluator);
                chainedContextType = evaluator.ReturnType;
            }

            return CollapseChainedEvaluators(evaluatorChain);
        }

        private static ExpressionEvaluator? BindSingleExpression(ReadOnlySpan<char> expression, Type objType)
        {
            try
            {
                if (expression.EndsWith("()", StringComparison.Ordinal))
                {
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
            catch
            {
                return null;
            }

        }

        private static ExpressionEvaluator? CollapseChainedEvaluators(List<ExpressionEvaluator> chain)
        {
            if (chain.Count == 0)
            {
                return null;
            }

            if (chain.Count == 1)
            {
                return chain[0];
            }

            return new ExpressionEvaluator(
                (obj) =>
                {
                    object? chainedResult = obj;
                    foreach (ExpressionEvaluator evaluator in chain)
                    {
                        if (chainedResult == null)
                        {
                            return null;
                        }

                        chainedResult = evaluator.Evaluator(chainedResult);
                    }

                    return chainedResult;
                },
                chain[^1].ReturnType);
        }
    }
}
