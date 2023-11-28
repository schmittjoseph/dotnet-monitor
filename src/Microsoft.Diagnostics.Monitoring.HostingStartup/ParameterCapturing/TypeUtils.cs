// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using Microsoft.Diagnostics.Monitoring.StartupHook.MonitorMessageDispatcher.Models;
using System;
using System.Diagnostics.CodeAnalysis;
using System.Text;

namespace Microsoft.Diagnostics.Monitoring.HostingStartup.ParameterCapturing
{
    internal static class TypeUtils
    {
        private static class Tokens
        {
            public const char ArityDelimiter = '`';

            public const char Separator = '.';
            public const char NestedSeparator = '+';

            public const char GenericStart = '[';
            public const char GenericEnd = ']';
        }

        public static bool IsSubType(string parentType, string typeToCheck)
        {
            if (string.IsNullOrEmpty(parentType))
            {
                throw new ArgumentException(nameof(parentType));
            }

            if (string.IsNullOrEmpty(typeToCheck))
            {
                throw new ArgumentException(nameof(typeToCheck));
            }

            if (!typeToCheck.StartsWith(parentType, StringComparison.Ordinal))
            {
                return false;
            }

            if (typeToCheck.Length == parentType.Length)
            {
                return true;
            }

            char charAfterParentType = typeToCheck[parentType.Length];
            return charAfterParentType is
                Tokens.Separator or
                Tokens.NestedSeparator or
                Tokens.ArityDelimiter;
        }

        public static bool TryStripGenerics(MethodDescription methodDescription, [NotNullWhen(true)] out MethodDescription? strippedMethodDescription)
        {
            strippedMethodDescription = null;

            if (!TryStripGenerics(methodDescription.TypeName, out string? newTypeName))
            {
                return false;
            }

            if (!TryStripGenerics(methodDescription.MethodName, out string? newMethodName))
            {
                return false;
            }

            strippedMethodDescription = new MethodDescription()
            {
                ModuleName = methodDescription.ModuleName,
                TypeName = newTypeName,
                MethodName = newMethodName,
            };
            return true;
        }

        internal static bool TryStripGenerics(string name, [NotNullWhen(true)] out string? strippedName)
        {
            strippedName = null;

            ArgumentNullException.ThrowIfNull(name);

            if (name.Length == 0)
            {
                strippedName = string.Empty;
                return true;
            }

            StringBuilder nameBuilder = new(name.Length);
            int depth = 0;

            for (int i = 0; i < name.Length; i++)
            {
                switch (name[i])
                {
                    case Tokens.GenericStart:
                        depth++;
                        break;
                    case Tokens.GenericEnd:
                        if (--depth < 0)
                        {
                            // Malformed
                            return false;
                        };
                        break;
                    default:
                        if (depth == 0)
                        {
                            nameBuilder.Append(name[i]);
                        }
                        break;
                }
            }

            if (depth != 0)
            {
                return false;
            }

            strippedName = nameBuilder.ToString();
            return true;
        }
    }
}
