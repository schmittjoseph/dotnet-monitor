// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using System;
using System.Collections.Immutable;
using System.Linq;
using System.Reflection.Metadata;
using System.Reflection.Metadata.Ecma335;
using static Microsoft.Diagnostics.Monitoring.HostingStartup.ParameterCapturing.BoxingTokens;

namespace Microsoft.Diagnostics.Monitoring.HostingStartup.ParameterCapturing
{
    /// <summary>
    /// This decoder is made specifically for parameters of the following types:
    /// - TypeReference'd value-types (e.g. enums from another assembly).
    ///
    /// The results of this decoder should not be used for any types not listed above.
    /// </summary>
    internal sealed class BoxingTokensSignatureProvider : ISignatureTypeProvider<uint, object?>
    {
        private readonly MetadataReader _mdReader;

        public  BoxingTokensSignatureProvider(MetadataReader reader)
        {
            _mdReader = reader;
        }

        //
        // Supported
        //
        public uint GetTypeFromDefinition(MetadataReader reader, TypeDefinitionHandle handle, byte rawTypeKind) => (uint)MetadataTokens.GetToken(handle);
        public uint GetTypeFromReference(MetadataReader reader, TypeReferenceHandle handle, byte rawTypeKind) => (uint)MetadataTokens.GetToken(handle);
        public uint GetTypeFromSpecification(MetadataReader reader, object? genericContext, TypeSpecificationHandle handle, byte rawTypeKind) => (uint)MetadataTokens.GetToken(handle);

        public uint GetPrimitiveType(PrimitiveTypeCode typeCode)
        {
            SpecialCaseBoxingTypes boxingType = typeCode switch
            {
                PrimitiveTypeCode.Boolean => SpecialCaseBoxingTypes.Boolean,
                PrimitiveTypeCode.Char => SpecialCaseBoxingTypes.Char,
                PrimitiveTypeCode.SByte => SpecialCaseBoxingTypes.SByte,
                PrimitiveTypeCode.Byte => SpecialCaseBoxingTypes.Byte,
                PrimitiveTypeCode.Int16 => SpecialCaseBoxingTypes.Int16,
                PrimitiveTypeCode.UInt16 => SpecialCaseBoxingTypes.UInt16,
                PrimitiveTypeCode.Int32 => SpecialCaseBoxingTypes.Int32,
                PrimitiveTypeCode.UInt32 => SpecialCaseBoxingTypes.UInt32,
                PrimitiveTypeCode.Int64 => SpecialCaseBoxingTypes.Int64,
                PrimitiveTypeCode.UInt64 => SpecialCaseBoxingTypes.UInt64,
                PrimitiveTypeCode.IntPtr => SpecialCaseBoxingTypes.IntPtr,
                PrimitiveTypeCode.UIntPtr => SpecialCaseBoxingTypes.UIntPtr,
                PrimitiveTypeCode.Single => SpecialCaseBoxingTypes.Single,
                PrimitiveTypeCode.Double => SpecialCaseBoxingTypes.Double,
                _ => SpecialCaseBoxingTypes.Unknown,
            };

            return boxingType.BoxingToken();
        }

        public uint GetGenericInstantiation(uint genericType, ImmutableArray<uint> typeArguments)
        {
            // Find the matching standalone signature
            int numStandaloneSigs = _mdReader.GetTableRowCount(TableIndex.StandAloneSig);
            for (int row = 1; row <= numStandaloneSigs; row++)
            {
                StandaloneSignatureHandle standaloneSigHandle = MetadataTokens.StandaloneSignatureHandle(row);
                if (standaloneSigHandle.IsNil)
                {
                    continue;
                }

                StandaloneSignature standaloneSignature = _mdReader.GetStandaloneSignature(standaloneSigHandle);
                if (standaloneSignature.GetKind() != StandaloneSignatureKind.LocalVariables)
                {
                    continue;
                }

                ImmutableArray<uint> decodedStandalone = standaloneSignature.DecodeLocalSignature(this, genericContext: null);
                if (!Enumerable.SequenceEqual(typeArguments, decodedStandalone))
                {
                    continue;
                }

                // Check the generic type
                var decodedReader = _mdReader.GetBlobReader(standaloneSignature.Signature);
                var decodedGenericType = MetadataTokens.GetToken(decodedReader.ReadTypeHandle());
                if (decodedGenericType != genericType)
                {
                    continue;
                }


                return (uint)MetadataTokens.GetToken(standaloneSigHandle);
            }

            return BoxingTokens.UnsupportedParameterToken;
        }

        //
        // Unsupported
        //
        public uint GetArrayType(uint elementType, ArrayShape shape) => BoxingTokens.UnsupportedParameterToken;
        public uint GetByReferenceType(uint elementType) => BoxingTokens.UnsupportedParameterToken;
        public uint GetFunctionPointerType(MethodSignature<uint> signature) => BoxingTokens.UnsupportedParameterToken;
        public uint GetGenericMethodParameter(object? genericContext, int index) => BoxingTokens.UnsupportedParameterToken;
        public uint GetGenericTypeParameter(object? genericContext, int index) => BoxingTokens.UnsupportedParameterToken;
        public uint GetModifiedType(uint modifier, uint unmodifiedType, bool isRequired) => BoxingTokens.UnsupportedParameterToken;
        public uint GetPinnedType(uint elementType) => BoxingTokens.UnsupportedParameterToken;
        public uint GetPointerType(uint elementType) => BoxingTokens.UnsupportedParameterToken;
        public uint GetSZArrayType(uint elementType) => BoxingTokens.UnsupportedParameterToken;
    }
}
