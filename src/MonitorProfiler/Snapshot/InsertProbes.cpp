// Copyright (c) .NET Foundation and contributors. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "cor.h"
#include "corprof.h"
#include "InsertProbes.h"
#include "ILRewriter.h"
#include <iostream>
#include <vector>

HRESULT DecompressNextSigComponent(
    IMetaDataEmit* pMetadataEmit,
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
            // JSFIX
        }
        else if (ulData == ELEMENT_TYPE_PINNED)
        {

        }
        else
        {
            hr = E_FAIL;
            goto ErrExit;
        }

        if (FAILED(DecompressNextSigComponent(pMetadataEmit, &pSignature[signatureCursor], signatureLength-signatureCursor, &cb, NULL, NULL)))
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
            if (FAILED(DecompressNextSigComponent(pMetadataEmit, &pSignature[signatureCursor], signatureLength-signatureCursor, &cb, NULL, NULL)))
                goto ErrExit;
            signatureCursor += cb;
        }

        elementType = static_cast<CorElementType>(ulData);

        goto ErrExit;
    }

    if (ulData == ELEMENT_TYPE_SZARRAY)
    {
        if (FAILED(DecompressNextSigComponent(pMetadataEmit, &pSignature[signatureCursor], signatureLength-signatureCursor, &cb, NULL, NULL)))
            goto ErrExit;
        signatureCursor += cb;

        elementType = static_cast<CorElementType>(ulData);

        goto ErrExit;
    }

    // instantiated type
    if (ulData == ELEMENT_TYPE_GENERICINST)
    {
        // display the type constructor
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
            if (FAILED(DecompressNextSigComponent(pMetadataEmit, &pSignature[signatureCursor], signatureLength-signatureCursor, &cb, NULL, NULL)))
                goto ErrExit;
            signatureCursor += cb;
            --numArgs;
        }

        if (tkChildElementType == ELEMENT_TYPE_VALUETYPE)
        {
            // Need to also resolve the token
            if (ptkType != NULL)
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
        if (FAILED(DecompressNextSigComponent(pMetadataEmit, &pSignature[signatureCursor], signatureLength-signatureCursor, &cb, NULL, NULL)))
            goto ErrExit;
        signatureCursor += cb;

        while (numArgs > 0)
        {
            if (signatureCursor > signatureLength)
                goto ErrExit;
            if (FAILED(DecompressNextSigComponent(pMetadataEmit, &pSignature[signatureCursor], signatureLength-signatureCursor, &cb, NULL, NULL)))
                goto ErrExit;
            signatureCursor += cb;
            --numArgs;
        }
        goto ErrExit;
    }

    if(ulData != ELEMENT_TYPE_ARRAY)
        return E_FAIL;

    elementType = static_cast<CorElementType>(ulData);

    // display the base type of SDARRAY
    if (FAILED(DecompressNextSigComponent(pMetadataEmit, &pSignature[signatureCursor], signatureLength-signatureCursor, &cb, NULL, NULL)))
        goto ErrExit;
    signatureCursor += cb;

    // display the rank of MDARRAY
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

    if (elementType != ELEMENT_TYPE_MAX && pElementType != NULL)
    {
        *pElementType = elementType;
    }

    if (ptkType != NULL)
    {
        *ptkType = tkType;
    }

    *pBytesRead = signatureCursor;
    return hr;
}

HRESULT ProcessArgs(IMetaDataImport* pMetadataImport, IMetaDataEmit* pMetadataEmit, PCCOR_SIGNATURE pbSigBlob, ULONG signatureLength, BOOL* hasThis, std::vector<std::pair<CorElementType, mdToken>>& paramTypes)
{
    HRESULT hr = S_OK;

    ULONG signatureCursor = 0;
    ULONG cb;
    ULONG ulData = NULL;
    ULONG ulArgs;

    *hasThis = FALSE;

    cb = CorSigUncompressData(pbSigBlob, &ulData);

    if (cb > signatureLength)
    {
        return E_FAIL;
    }
    signatureCursor += cb;
    signatureLength -= cb;

    if (ulData & IMAGE_CEE_CS_CALLCONV_HASTHIS ||
        ulData & IMAGE_CEE_CS_CALLCONV_EXPLICITTHIS)
    {
        *hasThis = TRUE;
    }

    if (isCallConv(ulData, IMAGE_CEE_CS_CALLCONV_FIELD))
    {
        // Do nothing
        return S_OK;
    }

    cb = CorSigUncompressData(&pbSigBlob[signatureCursor], &ulArgs);

    if (cb > signatureLength)
    {
        return E_FAIL;
    }
    signatureCursor += cb;
    signatureLength -= cb;

    if (ulData != IMAGE_CEE_CS_CALLCONV_LOCAL_SIG)
    {
        // Return type.
        IfFailRet(DecompressNextSigComponent(pMetadataEmit, &pbSigBlob[signatureCursor], signatureLength, &cb, NULL, NULL));

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
        ULONG ulDataUncompress;

        // Handle the sentinel for varargs because it isn't counted in the args.
        CorSigUncompressData(&pbSigBlob[signatureCursor], &ulDataUncompress);
    
        CorElementType elementType = ELEMENT_TYPE_END;
        mdToken tkType = mdTokenNil;
        IfFailRet(DecompressNextSigComponent(pMetadataEmit, &pbSigBlob[signatureCursor], signatureLength, &cb, &elementType, &tkType));

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

HRESULT GetTypeToBoxWith(IMetaDataImport* pMetadataImport, std::pair<CorElementType, mdToken> typeInfo, mdTypeDef* ptkBoxedType, struct CorLibTypeTokens * pCorLibTypeTokens)
{
    *ptkBoxedType = mdTypeDefNil;

    switch (typeInfo.first)
    {
    case ELEMENT_TYPE_ARRAY: // Arrays do not need to be boxed
        break;
    case ELEMENT_TYPE_BOOLEAN: // Bool
        *ptkBoxedType = pCorLibTypeTokens->tkSystemBooleanType;
        break;
    case ELEMENT_TYPE_CHAR: // Char
        *ptkBoxedType = pCorLibTypeTokens->tkSystemCharType;
        break;
    case ELEMENT_TYPE_CLASS: // Class; does not need to be boxed
        break;
    case ELEMENT_TYPE_FNPTR: // Delegate; does not need to be boxed
        break;
    case ELEMENT_TYPE_GENERICINST: // Instance of generic Type e.g. Tuple<int>
        // JSFIX
        // We should never actually reach here due to our resolution logic (CONFIRM).
        wprintf(L"UNSUPPORTED - ELEMENT_TYPE_GENERICINST\n");
        return E_FAIL;
    case ELEMENT_TYPE_I: // IntPtr
        *ptkBoxedType = pCorLibTypeTokens->tkSystemIntPtrType;
        break;
    case ELEMENT_TYPE_I1: // SByte
        *ptkBoxedType = pCorLibTypeTokens->tkSystemSByteType;
        break;
    case ELEMENT_TYPE_I2: // Short
        *ptkBoxedType = pCorLibTypeTokens->tkSystemInt16Type;
        break;
    case ELEMENT_TYPE_I4: // Int
        *ptkBoxedType = pCorLibTypeTokens->tkSystemInt32Type;
        break;
    case ELEMENT_TYPE_I8: // Long
        *ptkBoxedType = pCorLibTypeTokens->tkSystemInt64Type;
        break;
    case ELEMENT_TYPE_MVAR: // Generic method parameter
        // JSFIX -- test
        wprintf(L"UNSUPPORTED - ELEMENT_TYPE_MVAR\n");
        return E_FAIL;
    case ELEMENT_TYPE_OBJECT: // Object; does not need to be boxed
        break;
    case ELEMENT_TYPE_PTR: // Pointer; does not have boxing token but has special boxing instructions
        break;
    case ELEMENT_TYPE_R4: // Float
        *ptkBoxedType = pCorLibTypeTokens->tkSystemSingleType;
        break;
    case ELEMENT_TYPE_R8: // Double
        *ptkBoxedType = pCorLibTypeTokens->tkSystemDoubleType;
        break;
    case ELEMENT_TYPE_STRING: // String; does not need to be boxed
        break;
    case ELEMENT_TYPE_SZARRAY: // Array (single dimension, zero index); does not need to be boxed
        break;
    case ELEMENT_TYPE_U: // UIntPtr
        *ptkBoxedType = pCorLibTypeTokens->tkSystemUIntPtrType;
        break;
    case ELEMENT_TYPE_U1: // Byte
        *ptkBoxedType = pCorLibTypeTokens->tkSystemByteType;
        break;
    case ELEMENT_TYPE_U2: // UShort
        *ptkBoxedType = pCorLibTypeTokens->tkSystemUInt16Type;
        break;
    case ELEMENT_TYPE_U4: // UInt
        *ptkBoxedType = pCorLibTypeTokens->tkSystemUInt32Type;
        break;
    case ELEMENT_TYPE_U8: // ULong
        *ptkBoxedType = pCorLibTypeTokens->tkSystemUInt64Type;
        break;
    case ELEMENT_TYPE_VALUETYPE: { // Other value types (e.g. struct, enum, etc)
        WCHAR name[256];
        ULONG count = 0;
        if (TypeFromToken(typeInfo.second) == mdtTypeSpec) {
            wprintf(L"WE HAVE A TYPESPEC - 0x%0x\n", typeInfo.second);
        } else if (TypeFromToken(typeInfo.second) == mdtTypeDef) {
            wprintf(L"WE HAVE A TYPEDEF  - 0x%0x\n", typeInfo.second);
            IfFailRet(pMetadataImport->GetTypeDefProps(typeInfo.second, name, 256, &count, NULL, NULL));
            wprintf(L"valuetype name: %s\n", name);
        } else if (TypeFromToken(typeInfo.second) == mdtTypeRef) {
            wprintf(L"WE HAVE A TYPEREF - 0x%0x\n", typeInfo.second);
            IfFailRet(pMetadataImport->GetTypeRefProps(typeInfo.second, NULL, name, 256, &count));
            wprintf(L"valuetype name: %s\n", name);
        } else {
            return E_FAIL;
        }

        *ptkBoxedType = typeInfo.second;

        break;
    }
    case ELEMENT_TYPE_VAR: // Generic type parameter
        // JSFIX
        wprintf(L"UNSUPPORTED - ELEMENT_TYPE_VAR\n");
        return E_FAIL;
    default:
        return E_FAIL;
    }

    return S_OK;
}

HRESULT AddEnterProbe(
    ILRewriter * pilr,
    IMetaDataImport* pMetadataImport,
    IMetaDataEmit* pMetadataEmit,
    FunctionID functionId,
    mdMethodDef probeFunctionDef,
    ILInstr * pInsertProbeBeforeThisInstr,
    PCCOR_SIGNATURE sigParam,
    ULONG cbSigParam,
    struct CorLibTypeTokens * pCorLibTypeTokens)
{
    ILInstr * pNewInstr = nullptr;

    BOOL hasThis = FALSE;
    INT32 numArgs = 0;

    std::vector<std::pair<CorElementType, mdToken>> paramTypes;

    std::wcout << L"Trying to get args\n";
    IfFailRet(ProcessArgs(pMetadataImport, pMetadataEmit, sigParam, cbSigParam, &hasThis, paramTypes));
    std::wcout << L"--> Done\n";

    numArgs = (INT32)paramTypes.size();
    if (hasThis)
    {
       numArgs++;
    }

    /* Func Id */
    pNewInstr = pilr->NewILInstr();
    pNewInstr->m_opcode = CEE_LDC_I4;
    pNewInstr->m_Arg32 = (INT32)functionId;
    pilr->InsertBefore(pInsertProbeBeforeThisInstr, pNewInstr);

    /* Has This */
    pNewInstr = pilr->NewILInstr();
    pNewInstr->m_opcode = CEE_LDC_I4;
    pNewInstr->m_Arg32 = (INT32)hasThis;
    pilr->InsertBefore(pInsertProbeBeforeThisInstr, pNewInstr);

    /* Args */

    // Size of array
    pNewInstr = pilr->NewILInstr();
    pNewInstr->m_opcode = CEE_LDC_I4;
    pNewInstr->m_Arg32 = numArgs;
    pilr->InsertBefore(pInsertProbeBeforeThisInstr, pNewInstr);

    // Create the array
    pNewInstr = pilr->NewILInstr();
    pNewInstr->m_opcode = CEE_NEWARR;
    pNewInstr->m_Arg32 = pCorLibTypeTokens->tkSystemObjectType;
    pilr->InsertBefore(pInsertProbeBeforeThisInstr, pNewInstr);

    INT32 typeIndex = (hasThis) ? -1 : 0;
    for (INT32 i = 0; i < numArgs; i++)
    {
        // New entry on the evaluation stack
        pNewInstr = pilr->NewILInstr();
        pNewInstr->m_opcode = CEE_DUP;
        pilr->InsertBefore(pInsertProbeBeforeThisInstr, pNewInstr);

        // Index to set
        pNewInstr = pilr->NewILInstr();
        pNewInstr->m_opcode = CEE_LDC_I4;
        pNewInstr->m_Arg32 = i;
        pilr->InsertBefore(pInsertProbeBeforeThisInstr, pNewInstr);

        // Load arg
        pNewInstr = pilr->NewILInstr();
        pNewInstr->m_opcode = CEE_LDARG_S; // JSFIX: Arglist support
        pNewInstr->m_Arg32 = i;
        pilr->InsertBefore(pInsertProbeBeforeThisInstr, pNewInstr);

        // JSFIX: Validate -- this never needs to be boxed?
        if (typeIndex >= 0)
        { 
            auto typeInfo = paramTypes.at(typeIndex);

            mdTypeDef tkBoxedType = mdTypeDefNil;
            IfFailRet(GetTypeToBoxWith(pMetadataImport, typeInfo, &tkBoxedType, pCorLibTypeTokens));

            if (tkBoxedType != mdTypeDefNil)
            {
                pNewInstr = pilr->NewILInstr();
                pNewInstr->m_opcode = CEE_BOX;
                pNewInstr->m_Arg32 = tkBoxedType;
                pilr->InsertBefore(pInsertProbeBeforeThisInstr, pNewInstr);
            }
        }

        // Replace the i'th element in our new array with what we just pushed on the stack
        pNewInstr = pilr->NewILInstr();
        pNewInstr->m_opcode = CEE_STELEM_REF;
        pilr->InsertBefore(pInsertProbeBeforeThisInstr, pNewInstr);

        typeIndex++;
    }

    pNewInstr = pilr->NewILInstr();
    pNewInstr->m_opcode = CEE_CALL;
    pNewInstr->m_Arg32 = probeFunctionDef;
    pilr->InsertBefore(pInsertProbeBeforeThisInstr, pNewInstr);

    return S_OK;
}

HRESULT InsertProbes(
    ICorProfilerInfo * pICorProfilerInfo,
    IMetaDataImport* pMetadataImport,
    IMetaDataEmit* pMetadataEmit,
    ICorProfilerFunctionControl * pICorProfilerFunctionControl,
    ModuleID moduleID,
    mdMethodDef methodDef,
    FunctionID functionId,
    mdMethodDef enterProbeDef,
    mdMethodDef leaveProbeDef,
    PCCOR_SIGNATURE sigParam,
    ULONG cbSigParam,
    struct CorLibTypeTokens * pCorLibTypeTokens)
{
    ILRewriter rewriter(pICorProfilerInfo, pICorProfilerFunctionControl, moduleID, methodDef);

    IfFailRet(rewriter.Import());
    ILInstr* pFirstOriginalInstr = rewriter.GetILList()->m_pNext;
    IfFailRet(AddEnterProbe(&rewriter, pMetadataImport, pMetadataEmit, functionId, enterProbeDef, pFirstOriginalInstr, sigParam, cbSigParam, pCorLibTypeTokens));
    IfFailRet(rewriter.Export());

    return S_OK;
}