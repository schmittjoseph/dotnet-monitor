// Copyright (c) .NET Foundation and contributors. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#pragma once

#include "CorLibTypeTokens.h"
#include <vector>

class MethodSignatureParser
{
    public:
        static HRESULT GetMethodSignatureParamTypes(
            ComPtr<ICorProfilerInfo12> pCorProfilerInfo,
            ModuleID moduleId,
            mdMethodDef methodDef,
            BOOL& hasThis,
            std::vector<std::pair<CorElementType, mdToken>>& paramTypes);

    private:
        static HRESULT ReadMethodSignatureAndResolveTypes(
            ComPtr<IMetaDataEmit> pMetadataEmit,
            PCCOR_SIGNATURE pbSigBlob,
            ULONG signatureLength,
            BOOL& hasThis,
            std::vector<std::pair<CorElementType, mdToken>>& paramTypes);

        static HRESULT DecompressNextSigComponent(
            ComPtr<IMetaDataEmit> pMetadataEmit,
            PCCOR_SIGNATURE pSignature,
            ULONG signatureLength,
            ULONG *pBytesRead,
            BOOL* pIsPointerLike,
            CorElementType* pElementType,
            mdToken* ptkType);
};