// Copyright (c) .NET Foundation and contributors. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "cor.h"
#include "corprof.h"
#include "InsertProbes.h"
#include "ILRewriter.h"
#include <iostream>
#include "MethodSigParamExtractor.h"

HRESULT ProcessArgs2(IMetaDataImport* pMetadataImport, PCCOR_SIGNATURE pbSigBlob, ULONG ulSigBlob, BOOL* hasThis, std::vector<std::pair<CorElementType, mdToken>>& paramTypes)
{
    MethodSigParamExtractor extractor;

    if (!extractor.Parse((sig_byte *)pbSigBlob, ulSigBlob))
    {
        return E_FAIL;
    }

    std::vector<sig_elem_type> types = extractor.GetParamTypes();
    if (extractor.GetParamCount() != types.size())
    {
        wprintf(L"Invalid number of args %d - %zu\n", extractor.GetParamCount(), types.size());
        return E_FAIL;
    }

    *hasThis = extractor.GetHasThis();

    std::vector<std::pair<sig_index_type, sig_index>> metadataInfo = extractor.GetExtendedParamMetadataInfo();

    size_t pos = 0;
    for (auto e: types) {
        mdToken tkType = mdTokenNil;
        if (e == ELEMENT_TYPE_VALUETYPE) {
            std::pair<sig_index_type, sig_index> mdInfo = metadataInfo.at(pos);
            // JSFIX: Set mdInfo.first as msb

            tkType = 0x1000000 | mdInfo.second;
            pos++;
        }
        paramTypes.push_back({(CorElementType)e, tkType});
    }

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
        // JSFIX
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
            // WCHAR name[256];
            // ULONG count = 0;
            // IfFailRet(pMetadataImport->GetTypeDefProps(typeInfo.second, name, 256, &count, NULL, NULL));
            // wprintf(L"typedef name: %s\n", name);
        WCHAR name[256];
        ULONG count = 0;
        // Still need sig_index_type
        
        IfFailRet(pMetadataImport->GetTypeRefProps(typeInfo.second, NULL, name, 256, &count));

        // test
        wprintf(L"type name: %s - 0x%0x -- 0x%0x\n", name, typeInfo.second, pCorLibTypeTokens->tkSystemUInt64Type);

        // ComPtr<ITokenType> pTokenType;
        // IfFailRet(pType->QueryInterface(&pTokenType));
        // IfFailRet(pTokenType->GetToken(ptkBoxedType));
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
    IfFailRet(ProcessArgs2(pMetadataImport, sigParam, cbSigParam, &hasThis, paramTypes));
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
    IfFailRet(AddEnterProbe(&rewriter, pMetadataImport, functionId, enterProbeDef, pFirstOriginalInstr, sigParam, cbSigParam, pCorLibTypeTokens));
    IfFailRet(rewriter.Export());

    return S_OK;
}