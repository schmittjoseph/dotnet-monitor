// Copyright (c) .NET Foundation and contributors. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "cor.h"
#include "corprof.h"
#include "com.h"
#include "corhlpr.h"
#include "MethodSignatureParser.h"
#include <iostream>
#include <vector>

#include "JSFixUtils.h"

HRESULT MethodSignatureParser::GetMethodSignatureParamTypes(
    ComPtr<ICorProfilerInfo12> pCorProfilerInfo,
    ModuleID moduleId,
    mdMethodDef methodDef,
    BOOL& hasThis,
    std::vector<std::pair<CorElementType, mdToken>>& paramTypes)
{
    HRESULT hr;

    ComPtr<IMetaDataImport> pMetadataImport;
    hr = pCorProfilerInfo->GetModuleMetaData(moduleId, ofRead, IID_IMetaDataImport, reinterpret_cast<IUnknown **>(&pMetadataImport));
    if (hr != S_OK)
    {
        TEMPORARY_BREAK_ON_ERROR();
        return hr;
    }


    PCCOR_SIGNATURE pSignature;
    ULONG signatureLength;
    IfFailRet(pMetadataImport->GetMethodProps(methodDef, nullptr, nullptr, 0, nullptr, nullptr, &pSignature, &signatureLength, nullptr, nullptr));

    //
    // We need a metadata emitter to get tokens for typespecs.
    // In our case though we only want typespecs that should already be defined.
    // To guarantee we don't accidentally emit a new typespec into an assembly,
    // create a read-only emitter.
    //
    ComPtr<IMetaDataEmit> pMetadataEmit;
    hr = pCorProfilerInfo->GetModuleMetaData(moduleId, ofRead, IID_IMetaDataEmit, reinterpret_cast<IUnknown **>(&pMetadataEmit));
    if (hr != S_OK)
    {
        TEMPORARY_BREAK_ON_ERROR();
        return hr;
    }

    IfFailRet(ReadMethodSignatureAndResolveTypes(pMetadataEmit, pSignature, signatureLength, hasThis, paramTypes));

    return S_OK;
}

HRESULT MethodSignatureParser::ReadMethodSignatureAndResolveTypes(
    ComPtr<IMetaDataEmit> pMetadataEmit,
    PCCOR_SIGNATURE pSignature,
    ULONG signatureLength,
    BOOL& hasThis,
    std::vector<std::pair<CorElementType, mdToken>>& paramTypes)
{
    HRESULT hr = S_OK;

    ULONG signatureCursor = 0;
    ULONG cb;
    ULONG ulData = NULL;
    ULONG ulArgs;

    hasThis = FALSE;

    // §I.8.6.1.5
    cb = CorSigUncompressData(pSignature, &ulData);

    if (cb > signatureLength)
    {
        return E_FAIL;
    }
    signatureCursor += cb;
    signatureLength -= cb;

    if (ulData & IMAGE_CEE_CS_CALLCONV_HASTHIS ||
        ulData & IMAGE_CEE_CS_CALLCONV_EXPLICITTHIS)
    {
        hasThis = TRUE;
    }

    if (isCallConv(ulData, IMAGE_CEE_CS_CALLCONV_FIELD))
    {
        // Do nothing
        return S_OK;
    }

    cb = CorSigUncompressData(&pSignature[signatureCursor], &ulArgs);

    if (cb > signatureLength)
    {
        return E_FAIL;
    }
    signatureCursor += cb;
    signatureLength -= cb;

    if (ulData != IMAGE_CEE_CS_CALLCONV_LOCAL_SIG)
    {
        // Return type.
        IfFailRet(DecompressNextSigComponent(pMetadataEmit, &pSignature[signatureCursor], signatureLength, &cb, nullptr, nullptr));

        if (cb > signatureLength)
        {
            return E_FAIL;
        }

        signatureCursor += cb;
        signatureLength -= cb;
    }

    ULONG i = 0;
    while (i < ulArgs && signatureLength > 0)
    {
        // §I.8.6.1.4
        ULONG ulDataUncompress;

        // Handle the sentinel for varargs because it isn't counted in the args.
        CorSigUncompressData(&pSignature[signatureCursor], &ulDataUncompress);
    
        CorElementType elementType = ELEMENT_TYPE_END;
        mdToken tkType = mdTokenNil;
        IfFailRet(DecompressNextSigComponent(pMetadataEmit, &pSignature[signatureCursor], signatureLength, &cb, &elementType, &tkType));

        if (cb > signatureLength)
        {
            return E_FAIL;
        }

        paramTypes.push_back({elementType, tkType});

        signatureCursor += cb;
        signatureLength -= cb;
        i++;
    }

    cb = 0;

    return S_OK;
}

HRESULT MethodSignatureParser::DecompressNextSigComponent(
    ComPtr<IMetaDataEmit> pMetadataEmit,
    PCCOR_SIGNATURE pSignature,
    ULONG signatureLength,
    ULONG *pBytesRead,
    CorElementType* pElementType,
    mdToken* ptkType)
{
    HRESULT hr = S_OK;
    ULONG signatureCursor = 0;
    ULONG cb;
    ULONG ulData = ELEMENT_TYPE_MAX;
    ULONG ulTemp;
    mdToken tkType = mdTokenNil;

    CorElementType elementType = ELEMENT_TYPE_MAX;

    cb = CorSigUncompressData(pSignature, &ulData);
    signatureCursor += cb;

    // Handle the modifiers.
    if (ulData & ELEMENT_TYPE_MODIFIER)
    {
        if (ulData == ELEMENT_TYPE_SENTINEL)
        {
            // JSFIX: vargs
            FEATURE_USAGE_GUARD();
            hr = E_FAIL;
            goto ErrExit;
        }
        else if (ulData == ELEMENT_TYPE_PINNED)
        {

        }
        else
        {
            hr = E_FAIL;
            goto ErrExit;
        }

        if (FAILED(DecompressNextSigComponent(pMetadataEmit, &pSignature[signatureCursor], signatureLength-signatureCursor, &cb, nullptr, nullptr)))
        {
            goto ErrExit;
        }
        signatureCursor += cb;
        goto ErrExit;
    }


    // Handle the underlying element types.
    if (ulData >= ELEMENT_TYPE_MAX)
    {
        hr = E_FAIL;
        goto ErrExit;
    }

    while (ulData == ELEMENT_TYPE_PTR || ulData == ELEMENT_TYPE_BYREF)
    {
        cb = CorSigUncompressData(&pSignature[signatureCursor], &ulData);
        signatureCursor += cb;
    }

    if (CorIsPrimitiveType((CorElementType)ulData) ||
        ulData == ELEMENT_TYPE_TYPEDBYREF ||
        ulData == ELEMENT_TYPE_OBJECT ||
        ulData == ELEMENT_TYPE_I ||
        ulData == ELEMENT_TYPE_U)
    {
        // If this is a primitive type, we are done
        elementType = static_cast<CorElementType>(ulData);
        goto ErrExit;
    }

    if (ulData == ELEMENT_TYPE_VALUETYPE ||
        ulData == ELEMENT_TYPE_CLASS ||
        ulData == ELEMENT_TYPE_CMOD_REQD ||
        ulData == ELEMENT_TYPE_CMOD_OPT)
    {
        cb = CorSigUncompressToken(&pSignature[signatureCursor], &tkType);
        signatureCursor += cb;
    
        if (ulData == ELEMENT_TYPE_CMOD_REQD ||
            ulData == ELEMENT_TYPE_CMOD_OPT)
        {
            if (FAILED(DecompressNextSigComponent(pMetadataEmit, &pSignature[signatureCursor], signatureLength-signatureCursor, &cb, nullptr, nullptr)))
                goto ErrExit;
            signatureCursor += cb;
        }

        elementType = static_cast<CorElementType>(ulData);

        goto ErrExit;
    }

    if (ulData == ELEMENT_TYPE_SZARRAY)
    {
        if (FAILED(DecompressNextSigComponent(pMetadataEmit, &pSignature[signatureCursor], signatureLength-signatureCursor, &cb, nullptr, nullptr)))
            goto ErrExit;
        signatureCursor += cb;

        elementType = static_cast<CorElementType>(ulData);

        goto ErrExit;
    }

    // instantiated type
    if (ulData == ELEMENT_TYPE_GENERICINST)
    {
        // We need the nested type, but not the nested tk type.
        CorElementType tkChildElementType = ELEMENT_TYPE_MAX;
        mdToken tkCtor = mdTokenNil;

        ULONG start = signatureCursor - cb; // -cb to account for the element type

        if (FAILED(DecompressNextSigComponent(pMetadataEmit, &pSignature[signatureCursor], signatureLength-signatureCursor, &cb, &tkChildElementType, &tkCtor)))
            goto ErrExit;

        elementType = tkChildElementType;

        signatureCursor += cb;
        ULONG numArgs;
        cb = CorSigUncompressData(&pSignature[signatureCursor], &numArgs);
        signatureCursor += cb;

        while (numArgs > 0)
        {
            if (signatureCursor > signatureLength)
                goto ErrExit;
            if (FAILED(DecompressNextSigComponent(pMetadataEmit, &pSignature[signatureCursor], signatureLength-signatureCursor, &cb, nullptr, nullptr)))
                goto ErrExit;
            signatureCursor += cb;
            --numArgs;
        }

        if (tkChildElementType == ELEMENT_TYPE_VALUETYPE)
        {
            // Need to also resolve the token
            if (ptkType != nullptr)
            {
                mdTypeSpec tkTypeSpec = mdTokenNil;
                hr = pMetadataEmit->GetTokenFromTypeSpec(&pSignature[start], signatureCursor - start, &tkTypeSpec);
                if (hr != S_OK) {
                    hr = E_FAIL;
                    goto ErrExit;
                }
                tkType = tkTypeSpec;
            }
        }

        goto ErrExit;
    }

    if (ulData == ELEMENT_TYPE_VAR)
    {
        ULONG index;
        cb = CorSigUncompressData(&pSignature[signatureCursor], &index);
        signatureCursor += cb;
        elementType = static_cast<CorElementType>(ulData);

        goto ErrExit;
    }
    if (ulData == ELEMENT_TYPE_MVAR)
    {
        ULONG index;
        cb = CorSigUncompressData(&pSignature[signatureCursor], &index);
        signatureCursor += cb;
        elementType = static_cast<CorElementType>(ulData);

        goto ErrExit;
    }
    if (ulData == ELEMENT_TYPE_FNPTR)
    {
        elementType = static_cast<CorElementType>(ulData);

        cb = CorSigUncompressData(&pSignature[signatureCursor], &ulData);
        signatureCursor += cb;
    
        // Get number of args
        ULONG numArgs;
        cb = CorSigUncompressData(&pSignature[signatureCursor], &numArgs);
        signatureCursor += cb;

        // do return type
        if (FAILED(DecompressNextSigComponent(pMetadataEmit, &pSignature[signatureCursor], signatureLength-signatureCursor, &cb, nullptr, nullptr)))
            goto ErrExit;
        signatureCursor += cb;

        while (numArgs > 0)
        {
            if (signatureCursor > signatureLength)
                goto ErrExit;
            if (FAILED(DecompressNextSigComponent(pMetadataEmit, &pSignature[signatureCursor], signatureLength-signatureCursor, &cb, nullptr, nullptr)))
                goto ErrExit;
            signatureCursor += cb;
            --numArgs;
        }
        goto ErrExit;
    }

    if(ulData != ELEMENT_TYPE_ARRAY)
        return E_FAIL;

    elementType = static_cast<CorElementType>(ulData);

    // base type of SDARRAY
    if (FAILED(DecompressNextSigComponent(pMetadataEmit, &pSignature[signatureCursor], signatureLength-signatureCursor, &cb, nullptr, nullptr)))
        goto ErrExit;
    signatureCursor += cb;

    // rank of MDARRAY
    cb = CorSigUncompressData(&pSignature[signatureCursor], &ulData);
    signatureCursor += cb;
    if (ulData == 0)
        // we are done if no rank specified
        goto ErrExit;

    // how many dimensions have size specified?
    cb = CorSigUncompressData(&pSignature[signatureCursor], &ulData);
    signatureCursor += cb;
    while (ulData)
    {
        cb = CorSigUncompressData(&pSignature[signatureCursor], &ulTemp);
        signatureCursor += cb;
        ulData--;
    }
    // how many dimensions have lower bounds specified?
    cb = CorSigUncompressData(&pSignature[signatureCursor], &ulData);
    signatureCursor += cb;
    int discard;

    while (ulData)
    {
        cb = CorSigUncompressSignedInt(&pSignature[signatureCursor], &discard);
        signatureCursor += cb;
        ulData--;
    }

ErrExit:
    if (signatureCursor > signatureLength)
    {
        hr = E_FAIL;
    }

    if (elementType != ELEMENT_TYPE_MAX && pElementType != nullptr)
    {
        *pElementType = elementType;
    }

    if (ptkType != nullptr)
    {
        *ptkType = tkType;
    }

    *pBytesRead = signatureCursor;
    return hr;
}