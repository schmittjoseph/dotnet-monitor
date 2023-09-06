// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using System;
using System.Collections.Generic;
using System.Text;

namespace Microsoft.Diagnostics.Monitoring.HostingStartup.ParameterCapturing.ObjectFormatting.Formatters.DebuggerDisplay
{
    internal static class DebuggerDisplayParser
    {
        internal record ParsedDebuggerDisplay(string FormatString, Expression[] Expressions);
        internal record Expression(ReadOnlyMemory<char> ExpressionString, FormatSpecifier FormatSpecifier);

        internal static ParsedDebuggerDisplay? ParseDebuggerDisplay(string debuggerDisplay)
        {
            StringBuilder fmtString = new();
            List<Expression> expressions = new();

            for (int i = 0; i < debuggerDisplay.Length; i++)
            {
                char c = debuggerDisplay[i];
                switch (c)
                {
                    case '{':
                        Expression? parsedExpression = ParseExpression(debuggerDisplay.AsMemory(i), out int charsRead);
                        if (parsedExpression == null)
                        {
                            return null;
                        }
                        i += charsRead;

                        fmtString.Append('{');
                        fmtString.Append(expressions.Count);
                        fmtString.Append('}');

                        expressions.Add(parsedExpression);

                        break;
                    case '}':
                        // Malformed if observed here since above ParseExpression will chomp the expression's terminating '}'.
                        return null;

                    default:
                        fmtString.Append(c);
                        break;
                }
            }

            return new ParsedDebuggerDisplay(fmtString.ToString(), expressions.ToArray());
        }

        internal static Expression? ParseExpression(ReadOnlyMemory<char> expression, out int charsRead)
        {
            charsRead = 0;
            if (expression.Length == 0)
            {
                return null;
            }

            ReadOnlySpan<char> spanExpression = expression.Span;
            if (spanExpression[0] != '{')
            {
                return null;
            }
            spanExpression = spanExpression[1..];

            int formatSpecifiersStart = -1;

            int parenthesisDepth = 0;
            for (int i = 0; i < spanExpression.Length; i++)
            {
                charsRead++;
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
                                expression[1..(formatSpecifiersStart + 1)],
                                ParseFormatSpecifiers(spanExpression[formatSpecifiersStart..i]));
                        }

                        return new Expression(
                            expression[1..charsRead],
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

        internal static FormatSpecifier ParseFormatSpecifiers(ReadOnlySpan<char> specifiers)
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
                else if (specifier.Equals("nse", StringComparison.Ordinal))
                {
                    formatSpecifier |= FormatSpecifier.NoSideEffects;
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
