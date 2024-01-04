// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using Microsoft.Diagnostics.Monitoring.HostingStartup.ParameterCapturing.FunctionProbes;
using System;
using System.Collections.Immutable;
using System.Reflection.Metadata;
using System.Reflection.Metadata.Ecma335;

namespace Microsoft.Diagnostics.Monitoring.HostingStartup.ParameterCapturing
{
    /// <summary>
    /// Decodes signature blobs.
    /// See Metadata Specification section II.23.2: Blobs and signatures.
    /// </summary>
    internal static class SignatureDecoderEx
    {
        private static unsafe ParameterBoxingInstructions ReadParameterBoxingInstructions(ref SignatureDecoder<uint?, object?> decoder, ref BlobReader blobReader)
        {
            byte* start = blobReader.CurrentPointer;
            uint? mdToken = decoder.DecodeType(ref blobReader);

            if (mdToken.HasValue)
            {
                return mdToken.Value;
            }


            byte* end = blobReader.CurrentPointer;
            long size = end - start;
            if (size > uint.MaxValue)
            {
                throw new NotSupportedException();
            }

            return ParameterBoxingInstructions.FromSignature((IntPtr)start, end - start);
        }

        public static MethodSignature<ParameterBoxingInstructions> DecodeParameterBoxingInstructions(ref MetadataReader metadataReader, ref BlobReader blobReader)
        {
            SignatureDecoder<uint?, object?> decoder = new (new BoxingTokensSignatureProvider(), metadataReader, genericContext: null);

            SignatureHeader header = blobReader.ReadSignatureHeader();
            if (header.Kind != SignatureKind.Method)
            {
                throw new BadImageFormatException();
            }

            int genericParameterCount = 0;
            if (header.IsGeneric)
            {
                genericParameterCount = blobReader.ReadCompressedInteger();
            }

            int parameterCount = blobReader.ReadCompressedInteger();
            ParameterBoxingInstructions returnType = ReadParameterBoxingInstructions(ref decoder, ref blobReader);

            ImmutableArray<ParameterBoxingInstructions> parameterTypes;
            int requiredParameterCount;

            if (parameterCount == 0)
            {
                requiredParameterCount = 0;
                parameterTypes = ImmutableArray<ParameterBoxingInstructions>.Empty;
            }
            else
            {
                var parameterBuilder = ImmutableArray.CreateBuilder<ParameterBoxingInstructions>(parameterCount);
                int parameterIndex;

                for (parameterIndex = 0; parameterIndex < parameterCount; parameterIndex++)
                {
                    int curOffset = blobReader.Offset;

                    int typeCode = blobReader.ReadCompressedInteger();
                    if (typeCode == (int)SignatureTypeCode.Sentinel)
                    {
                        break;
                    }

                    // Rewind one compressed int as the DecodeType overload which accepts typeCode is private.
                    blobReader.Offset = curOffset;
                    parameterBuilder.Add(ReadParameterBoxingInstructions(ref decoder, ref blobReader));
                }

                requiredParameterCount = parameterIndex;
                for (; parameterIndex < parameterCount; parameterIndex++)
                {
                    parameterBuilder.Add(ReadParameterBoxingInstructions(ref decoder, ref blobReader));
                }
                parameterTypes = parameterBuilder.MoveToImmutable();
            }

            return new MethodSignature<ParameterBoxingInstructions>(header, returnType, requiredParameterCount, genericParameterCount, parameterTypes);
        }

    }
}
