// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using System;
using System.Collections;
using System.Reflection;
using System.Text;

namespace Microsoft.Diagnostics.Monitoring.HostingStartup.ParameterCapturing
{
    internal static class PrettyPrinter
    {
        public static void EmitSerializedObject(StringBuilder stringBuilder, object value)
        {
            if (value == null)
            {
                stringBuilder.Append("null");
                return;
            }

            //  else if (typeOverride.IsArray)
            // https://learn.microsoft.com/dotnet/csharp/programming-guide/arrays/multidimensional-arrays (rank)
            IEnumerable? enumerable = (value as IEnumerable);
            if (enumerable != null && value is not string)
            {
                stringBuilder.Append('[');
                int j = 0;
                foreach (object element in enumerable)
                {
                    if (j != 0)
                    {
                        stringBuilder.Append(", ");
                    }

                    if (j > 10)
                    {
                        EmitWrappedValue(stringBuilder, ParameterCapturingStrings.TruncatedArgumentValue, "{", "}");
                        break;
                    }

                    EmitSerializedObject(stringBuilder, element);
                    j++;
                }
                stringBuilder.Append(']');
                return;
            }

            if (value is IConvertible ic and not string)
            {
                stringBuilder.Append(ic.ToString(null) ?? string.Empty);
            }
            else if (value is IFormattable formattable)
            {
                EmitWrappedValue(stringBuilder, formattable.ToString(null, null));
            }
            else
            {
                EmitWrappedValue(stringBuilder, value.ToString());
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

            if (parameters.Length != supportedArgs.Length)
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
