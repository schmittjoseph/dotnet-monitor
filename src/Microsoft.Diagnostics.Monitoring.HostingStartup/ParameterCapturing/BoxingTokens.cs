// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Reflection;

namespace Microsoft.Diagnostics.Monitoring.HostingStartup.ParameterCapturing
{
    internal static class BoxingTokens
    {
        private const uint SpecialCaseBoxingTypeFlag = 0xff000000;
        private enum SpecialCaseBoxingTypes
        {
            Unknown = 0,
            Object,
            Boolean,
            Char,
            SByte,
            Byte,
            Int16,
            UInt16,
            Int32,
            UInt32,
            Int64,
            UInt64,
            Single,
            Double,
        };

        public static bool[] GetSupportedArgs(uint[] boxingTokens)
        {
            bool[] supportedArgs = new bool[boxingTokens.Length];
            for (int i = 0; i < boxingTokens.Length; i++)
            {
                supportedArgs[i] = boxingTokens[i] != GetBoxingType(SpecialCaseBoxingTypes.Unknown);
            }

            return supportedArgs;
        }

        public static uint[] GetBoxingTokens(MethodInfo method)
        {
            ParameterInfo[] methodParams = method.GetParameters();
            List<Type> methodParamTypes = methodParams.Select(p => p.ParameterType).ToList();
            List<uint> boxingTokens = new List<uint>(methodParams.Length);

            uint unsupported = GetBoxingType(SpecialCaseBoxingTypes.Unknown);
            uint skipBoxing = GetBoxingType(SpecialCaseBoxingTypes.Object);

            // Handle implicit this
            if (method.CallingConvention.HasFlag(CallingConventions.HasThis))
            {
                Debug.Assert(!method.IsStatic);

                Type? thisType = method.DeclaringType;
                if (thisType == null)
                {
                    boxingTokens.Add(unsupported);
                }
                else
                {
                    methodParamTypes.Insert(0, thisType);
                }
            }

            foreach (Type paramType in methodParamTypes)
            {
                if (paramType == null)
                {
                    boxingTokens.Add(unsupported);
                }
                else if (paramType.IsByRef ||
                    paramType.IsByRefLike ||
                    paramType.IsPointer)
                {
                    boxingTokens.Add(unsupported);
                }
                else if (paramType.IsPrimitive)
                {
                    boxingTokens.Add(GetBoxingType(Type.GetTypeCode(paramType)));
                }
                else if (paramType.IsValueType)
                {
                    // Ref structs have already been filtered out by the above IsByRefLike check.

                    if (paramType.IsGenericType)
                    {
                        // Typespec
                        boxingTokens.Add(unsupported);
                    }
                    else if (paramType.Assembly != method.Module.Assembly)
                    {
                        // Typeref
                        boxingTokens.Add(unsupported);
                    }
                    else
                    {
                        // Typedef
                        boxingTokens.Add((uint)paramType.MetadataToken);
                    }
                }
                else if (paramType.IsArray ||
                    paramType.HasMetadataToken())
                {
                    boxingTokens.Add(skipBoxing);
                }
                else
                {
                    boxingTokens.Add(unsupported);
                }
            }

            return boxingTokens.ToArray();
        }

        private static uint GetBoxingType(SpecialCaseBoxingTypes type)
        {
            return (SpecialCaseBoxingTypeFlag | (uint)type);
        }

        private static uint GetBoxingType(TypeCode typeCode)
        {
            return GetBoxingType(GetSpecialCaseBoxingType(typeCode));
        }

        private static SpecialCaseBoxingTypes GetSpecialCaseBoxingType(TypeCode typeCode)
        {
            return typeCode switch
            {
                TypeCode.Object => SpecialCaseBoxingTypes.Object,
                TypeCode.Boolean => SpecialCaseBoxingTypes.Boolean,
                TypeCode.Char => SpecialCaseBoxingTypes.Char,
                TypeCode.SByte => SpecialCaseBoxingTypes.SByte,
                TypeCode.Byte => SpecialCaseBoxingTypes.Byte,
                TypeCode.Int16 => SpecialCaseBoxingTypes.Int16,
                TypeCode.UInt16 => SpecialCaseBoxingTypes.UInt16,
                TypeCode.Int32 => SpecialCaseBoxingTypes.Int32,
                TypeCode.UInt32 => SpecialCaseBoxingTypes.UInt32,
                TypeCode.Int64 => SpecialCaseBoxingTypes.Int64,
                TypeCode.UInt64 => SpecialCaseBoxingTypes.UInt64,
                TypeCode.Single => SpecialCaseBoxingTypes.Single,
                TypeCode.Double => SpecialCaseBoxingTypes.Double,
                _ => SpecialCaseBoxingTypes.Unknown,
            };
        }
    }
}
