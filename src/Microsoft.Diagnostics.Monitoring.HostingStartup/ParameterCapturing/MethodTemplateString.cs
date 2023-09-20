// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using System;
using System.Reflection;
using System.Text;

namespace Microsoft.Diagnostics.Monitoring.HostingStartup.ParameterCapturing
{
    internal sealed class MethodTemplateString
    {
        private static class Tokens
        {
            private static class Internal
            {
                public const string Prefix = "<";
                public const string Postfix = ">";
            }

            public static class Types
            {
                public const char ArityDelimiter = '`';
                public const char Separator = '.';
                public const string Unknown = Internal.Prefix + "unknown" + Internal.Postfix;
            }

            public static class Parameters
            {
                public const char Start = '(';
                public const string Separator = ", ";
                public const string NameValueSeparator = ": ";
                public const char End = ')';

                public static class Modifiers
                {
                    public const char Separator = ' ';
                    public const string RefOrRefLike = "ref";
                    public const string In = "in";
                    public const string Out = "out";
                }

                public static class Names
                {
                    public const string ImplicitThis = "this";
                    public const string Unknown = Internal.Prefix + "unknown" + Internal.Postfix;
                }
            }

            public static class Generics
            {
                public const char Start = '<';
                public const string Separator = ", ";
                public const char End = '>';
            }
        }


        public string ModuleName { get; }
        public string TypeName { get; }
        public string MethodName { get; }

        public string Template { get; }

        public MethodTemplateString(MethodInfo method)
        {
            StringBuilder declaringTypeBuilder = new();
            StringBuilder methodNameBuilder = new();
            StringBuilder parametersTemplateBuilder = new();

            // Declaring type name
            // For a generic declaring type, trim the arity information and replace it with the known generic argument names.
            string declaringTypeName = method.DeclaringType?.FullName?.Split(Tokens.Types.ArityDelimiter)?[0] ?? Tokens.Types.Unknown;
            declaringTypeBuilder.Append(declaringTypeName);
            EmitGenericArguments(declaringTypeBuilder, method.DeclaringType?.GetGenericArguments());

            // Method name
            methodNameBuilder.Append(method.Name);
            EmitGenericArguments(methodNameBuilder, method.GetGenericArguments());

            // Method parameters
            int parameterIndex = 0;
            ParameterInfo[] explicitParameters = method.GetParameters();

            // Implicit this
            if (method.HasImplicitThis())
            {
                EmitParameter(
                    parametersTemplateBuilder,
                    method.DeclaringType,
                    Tokens.Parameters.Names.ImplicitThis);
                parameterIndex++;
            }

            foreach (ParameterInfo paramInfo in explicitParameters)
            {
                if (parameterIndex != 0)
                {
                    parametersTemplateBuilder.Append(Tokens.Parameters.Separator);
                }

                string name = paramInfo.Name ?? Tokens.Parameters.Names.Unknown;
                EmitParameter(
                    parametersTemplateBuilder,
                    paramInfo.ParameterType,
                    name,
                    paramInfo);

                parameterIndex++;
            }


            ModuleName = method.Module.Name;
            TypeName = declaringTypeBuilder.ToString();
            MethodName = methodNameBuilder.ToString();

            Template = string.Concat(
                TypeName,
                Tokens.Types.Separator,
                MethodName,
                Tokens.Parameters.Start,
                parametersTemplateBuilder.ToString(),
                Tokens.Parameters.End);
        }

        private static void EmitParameter(StringBuilder stringBuilder, Type? type, string name, ParameterInfo? paramInfo = null)
        {
            stringBuilder.AppendLine();
            stringBuilder.Append('\t');

            // Modifiers
            if (paramInfo?.IsIn == true)
            {
                stringBuilder.Append(Tokens.Parameters.Modifiers.In);
                stringBuilder.Append(Tokens.Parameters.Modifiers.Separator);
            }
            else if (paramInfo?.IsOut == true)
            {
                stringBuilder.Append(Tokens.Parameters.Modifiers.Out);
                stringBuilder.Append(Tokens.Parameters.Modifiers.Separator);
            }
            else if (type?.IsByRef == true ||
                    type?.IsByRefLike == true)
            {
                stringBuilder.Append(Tokens.Parameters.Modifiers.RefOrRefLike);
                stringBuilder.Append(Tokens.Parameters.Modifiers.Separator);
            }

            // Name
            stringBuilder.Append(name);
            stringBuilder.Append(Tokens.Parameters.NameValueSeparator);

            // Value
            EmitFormatItem(stringBuilder, name);
        }

        private static void EmitGenericArguments(StringBuilder stringBuilder, Type[]? genericArgs)
        {
            if (genericArgs == null || genericArgs.Length == 0)
            {
                return;
            }

            stringBuilder.Append(Tokens.Generics.Start);
            for (int i = 0; i < genericArgs.Length; i++)
            {
                if (i != 0)
                {
                    stringBuilder.Append(Tokens.Generics.Separator);
                }

                stringBuilder.Append(genericArgs[i].Name);
            }
            stringBuilder.Append(Tokens.Generics.End);
        }

        private static void EmitFormatItem(StringBuilder stringBuilder, string name)
        {
            stringBuilder.Append('{');
            stringBuilder.Append(name);
            stringBuilder.Append('}');
        }
    }
}
