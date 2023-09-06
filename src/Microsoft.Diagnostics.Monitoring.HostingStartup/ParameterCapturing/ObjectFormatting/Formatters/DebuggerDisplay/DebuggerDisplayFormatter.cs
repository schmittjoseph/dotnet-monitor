// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Reflection;
using static Microsoft.Diagnostics.Monitoring.HostingStartup.ParameterCapturing.ObjectFormatting.Formatters.DebuggerDisplay.DebuggerDisplayParser;

namespace Microsoft.Diagnostics.Monitoring.HostingStartup.ParameterCapturing.ObjectFormatting.Formatters.DebuggerDisplay
{
    internal static class DebuggerDisplayFormatter
    {
        internal record DebuggerDisplayAttributeValue(string Value, IEnumerable<Type> EncompassingTypes);

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

            ParsedDebuggerDisplay? parsedDebuggerDiplay = DebuggerDisplayParser.ParseDebuggerDisplay(attribute.Value);
            if (parsedDebuggerDiplay == null)
            {
                return null;
            }

            ObjectFormatterFunc? formatter = ExpressionBinder.BindParsedDebuggerDisplay(objType, parsedDebuggerDiplay);
            if (formatter == null)
            {
                return null;
            }

            return new FormatterFactoryResult(formatter, attribute.EncompassingTypes);
        }

        internal static DebuggerDisplayAttributeValue? GetDebuggerDisplayAttribute(Type objType)
        {
            List<Type> encompassingTypes = new();

            Type? currentType = objType;
            while (currentType != null)
            {
                encompassingTypes.Add(currentType);

                DebuggerDisplayAttribute? attribute = currentType.GetCustomAttribute<DebuggerDisplayAttribute>(inherit: false);
                if (attribute?.Value != null)
                {
                    return new DebuggerDisplayAttributeValue(attribute.Value, encompassingTypes);
                }
                currentType = currentType.BaseType;
            }

            encompassingTypes.Clear();
            return null;
        }
    }
}
