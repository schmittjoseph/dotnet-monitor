// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using System;
using System.Reflection;
using System.Text;

namespace Microsoft.Diagnostics.Monitoring.HostingStartup.ParameterCapturing
{
    internal static class PrettyPrinter
    {
        public static string SerializeObject(object value)
        {
            if (value == null)
            {
                return ParameterCapturingStrings.NullArgumentValue;
            }

            try
            {
                bool doWrapValue = false;
                string serializedValue;
                if (value is IConvertible ic)
                {
                    serializedValue = ic.ToString(provider: null);
                    doWrapValue = (value is string);
                }
                else if (value is IFormattable formattable)
                {
                    serializedValue = formattable.ToString(format: null, formatProvider: null);
                    doWrapValue = true;
                }
                else
                {
                    serializedValue = value.ToString() ?? string.Empty;
                    doWrapValue = true;
                }

                return doWrapValue ? string.Concat('\'', serializedValue, '\'') : serializedValue;
            }
            catch
            {
                return ParameterCapturingStrings.UnknownArgumentValue;
            }
        }

        public static string? ConstructFormattableStringFromMethod(MethodInfo method, bool[] supportedParameters)
        {
            StringBuilder fmtStringBuilder = new();

            string className = method.DeclaringType?.FullName?.Split('`')?[0] ?? string.Empty;
            fmtStringBuilder.Append(className);
            EmitGenericArguments(fmtStringBuilder, method.DeclaringType?.GetGenericArguments());

            fmtStringBuilder.Append($".{method.Name}");
            EmitGenericArguments(fmtStringBuilder, method.GetGenericArguments());

            fmtStringBuilder.Append('(');

            int fmtIndex = 0;
            int index = 0;
            ParameterInfo[] explicitParameters = method.GetParameters();

            int numberOfParameters = explicitParameters.Length + (method.CallingConvention.HasFlag(CallingConventions.HasThis) ? 1 : 0);
            if (numberOfParameters != supportedParameters.Length)
            {
                return null;
            }

            if (method.CallingConvention.HasFlag(CallingConventions.HasThis))
            {

                if (EmitParameter(fmtStringBuilder, method.DeclaringType, "this", supportedParameters[index], fmtIndex))
                {
                    fmtIndex++;
                }
                index++;
            }

            foreach (ParameterInfo paramInfo in explicitParameters)
            {
                if (index != 0 || fmtIndex != 0)
                {
                    fmtStringBuilder.Append(", ");
                }

                if (EmitParameter(fmtStringBuilder, paramInfo.ParameterType, paramInfo.Name, supportedParameters[index], fmtIndex, paramInfo))
                {
                    fmtIndex++;
                }

                index++;
            }

            fmtStringBuilder.Append(')');

            return fmtStringBuilder.ToString();
        }

        private static bool EmitParameter(StringBuilder stringBuilder, Type? type, string? name, bool isSupported, int formatIndex, ParameterInfo? paramInfo = null)
        {
            stringBuilder.AppendLine();
            stringBuilder.Append('\t');

            // Modifiers
            if (paramInfo?.IsIn == true)
            {
                stringBuilder.Append("in ");
            }
            else if (paramInfo?.IsOut == true)
            {
                stringBuilder.Append("out ");
            }
            else if (type?.IsByRef == true ||
                    type?.IsByRefLike == true)
            {
                stringBuilder.Append("ref ");
            }

            // Name
            if (string.IsNullOrEmpty(name))
            {
                EmitResourceString(stringBuilder, ParameterCapturingStrings.UnknownParameterName);
            }
            else
            {
                stringBuilder.Append(name);
            }

            stringBuilder.Append(": ");

            // Value
            if (isSupported)
            {
                stringBuilder.Append('{');
                stringBuilder.Append(formatIndex);
                stringBuilder.Append('}');
                return true;
            }
            else
            {
                EmitResourceString(stringBuilder, ParameterCapturingStrings.UnsupportedParameter);
                return false;
            }
        }

        private static void EmitGenericArguments(StringBuilder stringBuilder, Type[]? genericArgs)
        {
            if (genericArgs == null || genericArgs.Length == 0)
            {
                return;
            }

            stringBuilder.Append('<');
            for (int i = 0; i < genericArgs.Length; i++)
            {
                if (i != 0)
                {
                    stringBuilder.Append(", ");
                }

                stringBuilder.Append(genericArgs[i].Name);
            }
            stringBuilder.Append('>');
        }

        private static void EmitResourceString(StringBuilder stringBuilder, string value)
        {
            stringBuilder.Append("{{");
            stringBuilder.Append(value);
            stringBuilder.Append("}}");
        }
    }
}
