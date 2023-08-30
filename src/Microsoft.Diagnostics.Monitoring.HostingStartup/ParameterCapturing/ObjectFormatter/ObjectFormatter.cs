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

        private static string WrapValue(string value) => string.Concat(
            MethodTemplateStringGenerator.Tokens.Parameters.Values.WrappedStart,
            value,
            MethodTemplateStringGenerator.Tokens.Parameters.Values.WrappedEnd);

        private static string IConvertibleFormatter(object obj) => ((IConvertible)obj).ToString(CultureInfo.InvariantCulture);

        private static string IFormattableFormatter(object obj) => WrapValue(((IFormattable)obj).ToString(format: null, CultureInfo.InvariantCulture));

        private static string GeneralFormatter(object obj) => WrapValue(obj.ToString() ?? string.Empty);

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

            if (objType.IsAssignableTo(typeof(IConvertible)))
            {
                if (objType == typeof(string))
                {
                    return new GeneratedFormatter(GeneralFormatter, new[] { objType });
                }
                return new GeneratedFormatter(IConvertibleFormatter, new[] { objType });
            }
            else if (objType.IsAssignableTo(typeof(IFormatProvider)))
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
