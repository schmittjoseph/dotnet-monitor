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
                return "null";
            }

            if (value is IConvertible ic and not string)
            {
                return ic.ToString(null) ?? string.Empty;
            }
            else if (value is IFormattable formattable)
            {
                return string.Concat('\'', formattable.ToString(null, null), '\'');
            }
            else
            {
                return string.Concat('\'', value.ToString(), '\'');
            }
        }

        public static string? ConstructFormattableStringFromMethod(MethodInfo method, bool[] supportedArgs)
        {
            StringBuilder fmtStringBuilder = new();

            string className = method.DeclaringType?.FullName?.Split('`')?[0] ?? string.Empty;
            fmtStringBuilder.Append(className);
            PrettyPrintGenericArgs(fmtStringBuilder, method.DeclaringType?.GetGenericArguments());

            fmtStringBuilder.Append($".{method.Name}");
            PrettyPrintGenericArgs(fmtStringBuilder, method.GetGenericArguments());

            fmtStringBuilder.Append('(');

            int fmtIndex = 0;
            int index = 0;
            ParameterInfo[] parameters = method.GetParameters();

            int numberOfParameters = parameters.Length + (method.CallingConvention.HasFlag(CallingConventions.HasThis) ? 1 : 0);
            if (numberOfParameters != supportedArgs.Length)
            {
                return null;
            }

            if (method.CallingConvention.HasFlag(CallingConventions.HasThis))
            {

                if (EmitParameter(fmtStringBuilder, method.DeclaringType, "this", supportedArgs[index], fmtIndex))
                {
                    fmtIndex++;
                }
                index++;
            }

            foreach (ParameterInfo paramInfo in parameters)
            {
                if (fmtIndex != 0)
                {
                    fmtStringBuilder.Append(", ");
                }

                if (EmitParameter(fmtStringBuilder, paramInfo.ParameterType, paramInfo.Name, supportedArgs[index], fmtIndex, paramInfo))
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
            if (paramInfo?.IsOut == true)
            {
                stringBuilder.Append("out ");
            }
            else if (type?.IsByRefLike == true)
            {
                stringBuilder.Append("ref struct ");
            }
            else if (type?.IsByRef == true)
            {
                stringBuilder.Append("ref ");
            }

            if (string.IsNullOrEmpty(name))
            {
                EmitWrappedValue(stringBuilder, ParameterCapturingStrings.UnknownArgumentName, "{{", "}}");
            }
            else
            {
                stringBuilder.Append(name);
            }

            stringBuilder.Append(": ");

            if (isSupported)
            {
                stringBuilder.Append('{');
                stringBuilder.Append(formatIndex);
                stringBuilder.Append('}');
                return true;
            }
            else
            {
                EmitWrappedValue(stringBuilder, ParameterCapturingStrings.UnsupportedArgument, "{{", "}}");
                return false;
            }
        }

        private static void PrettyPrintGenericArgs(StringBuilder stringBuilder, Type[]? genericArgs)
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

        private static void EmitWrappedValue(StringBuilder stringBuilder, string? value, string prefix = "'", string postfix = "'")
        {
            stringBuilder.Append(prefix);
            stringBuilder.Append(value ?? string.Empty);
            stringBuilder.Append(postfix);
        }
    }
}
