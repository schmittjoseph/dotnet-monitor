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

            if (method.CallingConvention.HasFlag(CallingConventions.HasThis))
            {
                Debug.Assert(!method.IsStatic);

                Type? contextfulType = method.DeclaringType;
                if (contextfulType == null)
                {
                    boxingTokens.Add(unsupported);
                }
                else
                {
                    methodParamTypes.Insert(0, contextfulType);
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
                else if (paramType.HasMetadataToken() ||
                        paramType.IsArray)
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


        private static uint GetBoxingType(TypeCode typeCode)
        {
            return (SpecialCaseBoxingTypeFlag | (uint)GetSpecialCaseBoxingType(typeCode));
        }

        private static uint GetBoxingType(SpecialCaseBoxingTypes type)
        {
            return (SpecialCaseBoxingTypeFlag | (uint)type);
        }

        private static SpecialCaseBoxingTypes GetSpecialCaseBoxingType(TypeCode typeCode)
        {
            switch (typeCode)
            {
                case TypeCode.Object:
                    return SpecialCaseBoxingTypes.Object;
                case TypeCode.Boolean:
                    return SpecialCaseBoxingTypes.Boolean;
                case TypeCode.Char:
                    return SpecialCaseBoxingTypes.Char;
                case TypeCode.SByte:
                    return SpecialCaseBoxingTypes.SByte;
                case TypeCode.Byte:
                    return SpecialCaseBoxingTypes.Byte;
                case TypeCode.Int16:
                    return SpecialCaseBoxingTypes.Int16;
                case TypeCode.UInt16:
                    return SpecialCaseBoxingTypes.UInt16;
                case TypeCode.Int32:
                    return SpecialCaseBoxingTypes.Int32;
                case TypeCode.UInt32:
                    return SpecialCaseBoxingTypes.UInt32;
                case TypeCode.Int64:
                    return SpecialCaseBoxingTypes.Int64;
                case TypeCode.UInt64:
                    return SpecialCaseBoxingTypes.UInt64;
                case TypeCode.Single:
                    return SpecialCaseBoxingTypes.Single;
                case TypeCode.Double:
                    return SpecialCaseBoxingTypes.Double;
                default:
                    return SpecialCaseBoxingTypes.Unknown;
            }
        }
    }
}
