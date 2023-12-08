// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using System;
using System.Collections.Generic;
using System.Reflection.Metadata;
using System.Reflection.Metadata.Ecma335;

namespace Microsoft.Diagnostics.Monitoring.HostingStartup.ParameterCapturing
{
    internal static class SignatureUtils
    {
        public unsafe record ParameterSignature(byte[] blob, uint BoxingToken);

        // section II.23.2
        public static unsafe List<ParameterSignature> ExtractParameterSignatures(ref BlobReader blobReader)
        {
            List<ParameterSignature> paramSignatures = new();

            SignatureHeader header = blobReader.ReadSignatureHeader();
            CheckMethodOrPropertyHeader(header);

            if (header.IsGeneric)
            {
                blobReader.ReadCompressedInteger();
            }

            int parameterCount = blobReader.ReadCompressedInteger();
            // Return type
            _ = DecodeType(ref blobReader);

            int parameterIndex;
            for (parameterIndex = 0; parameterIndex < parameterCount; parameterIndex++)
            {
                byte* parameterBlobStart = blobReader.CurrentPointer;
                int typeCode = blobReader.ReadCompressedInteger();
                if (typeCode == (int)SignatureTypeCode.Sentinel)
                {
                    break;
                }
                uint boxingToken = DecodeType(ref blobReader, typeCode: typeCode);
                byte* parameterBlobEnd = blobReader.CurrentPointer;

                paramSignatures.Add(new ParameterSignature(new Span<byte>(parameterBlobStart, (int)(parameterBlobEnd - parameterBlobStart)).ToArray(), boxingToken));
            }

            for (; parameterIndex < parameterCount; parameterIndex++)
            {
                byte* parameterBlobStart = blobReader.CurrentPointer;
                uint boxingToken = DecodeType(ref blobReader);
                byte* parameterBlobEnd = blobReader.CurrentPointer;

                paramSignatures.Add(new ParameterSignature(new Span<byte>(parameterBlobStart, (int)(parameterBlobEnd - parameterBlobStart)).ToArray(), boxingToken));
            }

            return paramSignatures;
        }

        private static uint DecodeType(ref BlobReader blobReader)
        {
            return DecodeType(ref blobReader, blobReader.ReadCompressedInteger());
        }

        private static uint DecodeType(ref BlobReader blobReader, int typeCode)
        {
            switch (typeCode)
            {
                case (int)SignatureTypeCode.Boolean:
                case (int)SignatureTypeCode.Char:
                case (int)SignatureTypeCode.SByte:
                case (int)SignatureTypeCode.Byte:
                case (int)SignatureTypeCode.Int16:
                case (int)SignatureTypeCode.UInt16:
                case (int)SignatureTypeCode.Int32:
                case (int)SignatureTypeCode.UInt32:
                case (int)SignatureTypeCode.Int64:
                case (int)SignatureTypeCode.UInt64:
                case (int)SignatureTypeCode.Single:
                case (int)SignatureTypeCode.Double:
                case (int)SignatureTypeCode.IntPtr:
                case (int)SignatureTypeCode.UIntPtr:
                case (int)SignatureTypeCode.Object:
                case (int)SignatureTypeCode.String:
                case (int)SignatureTypeCode.Void:
                case (int)SignatureTypeCode.TypedReference:
                    break;

                case (int)SignatureTypeCode.Pointer:
                case (int)SignatureTypeCode.ByReference:
                case (int)SignatureTypeCode.Pinned:
                case (int)SignatureTypeCode.SZArray:
                    DecodeType(ref blobReader);
                    break;


                case (int)SignatureTypeCode.FunctionPointer:
                    _ = ExtractParameterSignatures(ref blobReader);
                    break;

                case (int)SignatureTypeCode.Array:
                    DecodeArrayType(ref blobReader);
                    break;

                case (int)SignatureTypeCode.RequiredModifier:
                    DecodeModifiedType(ref blobReader);
                    break;

                case (int)SignatureTypeCode.OptionalModifier:
                    DecodeModifiedType(ref blobReader);
                    break;

                case (int)SignatureTypeCode.GenericTypeInstance:
                    DecodeGenericTypeInstance(ref blobReader);
                    break;

                case (int)SignatureTypeCode.GenericTypeParameter:
                    blobReader.ReadCompressedInteger();
                    break;

                case (int)SignatureTypeCode.GenericMethodParameter:
                    blobReader.ReadCompressedInteger();
                    break;

                case (int)SignatureTypeKind.Class:
                case (int)SignatureTypeKind.ValueType:
                    return DecodeTypeHandle(ref blobReader);

                default:
                    throw new BadImageFormatException();
            }

            return BoxingTokens.UnsupportedParameterToken;
        }

        private static void DecodeTypeSequence(ref BlobReader blobReader)
        {
            int count = blobReader.ReadCompressedInteger();
            if (count == 0)
            {
                // This method is used for Local signatures and method specs, neither of which can have
                // 0 elements. Parameter sequences can have 0 elements, but they are handled separately
                // to deal with the sentinel/varargs case.
                throw new BadImageFormatException();
            }

            for (int i = 0; i < count; i++)
            {
                DecodeType(ref blobReader);
            }
        }

        private static void DecodeArrayType(ref BlobReader blobReader)
        {
            DecodeType(ref blobReader);
            blobReader.ReadCompressedInteger();

            int sizesCount = blobReader.ReadCompressedInteger();
            if (sizesCount > 0)
            {
                for (int i = 0; i < sizesCount; i++)
                {
                    blobReader.ReadCompressedInteger();
                }
            }

            int lowerBoundsCount = blobReader.ReadCompressedInteger();
            if (lowerBoundsCount > 0)
            {
                for (int i = 0; i < lowerBoundsCount; i++)
                {
                    blobReader.ReadCompressedSignedInteger();
                }
            }
        }

        private static void DecodeGenericTypeInstance(ref BlobReader blobReader)
        {
            DecodeType(ref blobReader);
            DecodeTypeSequence(ref blobReader);
        }

        private static void DecodeModifiedType(ref BlobReader blobReader)
        {
            _ = DecodeTypeHandle(ref blobReader);
            DecodeType(ref blobReader);
        }

        private static uint DecodeTypeHandle(ref BlobReader blobReader)
        {
            EntityHandle handle = blobReader.ReadTypeHandle();
            if (!handle.IsNil)
            {
                switch (handle.Kind)
                {
                    case HandleKind.TypeDefinition:
                    case HandleKind.TypeReference:
                        return (uint)MetadataTokens.GetToken(handle);

                    default:
                        return BoxingTokens.UnsupportedParameterToken;
                }
            }

            throw new BadImageFormatException();
        }

        private static void CheckMethodOrPropertyHeader(SignatureHeader header)
        {
            SignatureKind kind = header.Kind;
            if (kind != SignatureKind.Method && kind != SignatureKind.Property)
            {
                throw new BadImageFormatException();
            }
        }
    }
}
