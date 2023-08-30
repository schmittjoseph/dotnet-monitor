// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using System;
using System.Collections.Generic;
using System.Globalization;

namespace Microsoft.Diagnostics.Monitoring.HostingStartup.ParameterCapturing.ObjectFormatter
{
    internal static class ObjectFormatter
    {
        public delegate string Formatter(object obj);
        public record GeneratedFormatter(Formatter Formatter, IEnumerable<Type> EncompassingTypes);

        private static string IConvertibleFormatter(object obj) => ((IConvertible)obj).ToString(CultureInfo.InvariantCulture);
        private static string IFormattableFormatter(object obj) => ((IFormattable)obj).ToString(format: null, CultureInfo.InvariantCulture);
        private static string GeneralFormatter(object obj) => string.Concat(
            MethodTemplateStringGenerator.Tokens.Parameters.Values.WrappedStart,
            obj.ToString() ?? string.Empty,
            MethodTemplateStringGenerator.Tokens.Parameters.Values.WrappedEnd);

        public static GeneratedFormatter GetFormatter(Type objType, bool useDebuggerDisplayAttribute)
        {
            if (useDebuggerDisplayAttribute)
            {
                GeneratedFormatter? debuggerDisplayFormatter = DebuggerDisplay.GetDebuggerDisplayFormatter(objType);
                if (debuggerDisplayFormatter != null)
                {
                    return debuggerDisplayFormatter;
                }
            }

            if (objType is IConvertible)
            {
                return new GeneratedFormatter(IConvertibleFormatter, new[] { objType });
            }
            else if (objType is IFormattable)
            {
                return new GeneratedFormatter(IFormattableFormatter, new[] { objType });
            }
            else
            {
                return new GeneratedFormatter(GeneralFormatter, new[] { objType });
            }
        }
    }
}
