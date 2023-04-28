// Copyright (c) .NET Foundation and contributors. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "cor.h"
#include "corprof.h"
#include "ProbeInjector.h"
#include "ILRewriter.h"
#include <iostream>
#include <vector>

#include "JSFixUtils.h"


typedef enum TypeCode
{
    TYPE_CODE_EMPTY             = 0x00,
    TYPE_CODE_OBJECT            = 0x01,
    TYPE_CODE_DB_NULL           = 0x02,
    TYPE_CODE_BOOLEAN           = 0x03,
    TYPE_CODE_CHAR              = 0x04,
    TYPE_CODE_SBYTE             = 0x05,
    TYPE_CODE_BYTE              = 0x06,
    TYPE_CODE_INT16             = 0x07,
    TYPE_CODE_UINT16            = 0x08,
    TYPE_CODE_INT32             = 0x09,
    TYPE_CODE_UINT32            = 0x0a,
    TYPE_CODE_INT64             = 0x0b,
    TYPE_CODE_UINT64            = 0x0c,
    TYPE_CODE_SINGLE            = 0x0d,
    TYPE_CODE_DOUBLE            = 0x0e,
    TYPE_CODE_DECIMAL           = 0x0f,
    TYPE_CODE_DATE_TIME         = 0x10,
    // No 0x11
    TYPE_CODE_STRING            = 0x12
} TypeCode;

HRESULT GetBoxingType(
    UINT32 typeInfo,
    mdToken* ptkBoxedType,
    struct CorLibTypeTokens* pCorLibTypeTokens)
{
    *ptkBoxedType = mdTypeDefNil;

    switch(typeInfo)
    {
    case TypeCode::TYPE_CODE_BOOLEAN:
        *ptkBoxedType = pCorLibTypeTokens->tkSystemBooleanType;
        break;
    case TypeCode::TYPE_CODE_BYTE:
        *ptkBoxedType = pCorLibTypeTokens->tkSystemByteType;
        break;
    case TypeCode::TYPE_CODE_CHAR:
        *ptkBoxedType = pCorLibTypeTokens->tkSystemCharType;
        break;
    case TypeCode::TYPE_CODE_DOUBLE:
        *ptkBoxedType = pCorLibTypeTokens->tkSystemDoubleType;
        break;
    case TypeCode::TYPE_CODE_INT16:
        *ptkBoxedType = pCorLibTypeTokens->tkSystemInt16Type;
        break;
    case TypeCode::TYPE_CODE_INT32:
        *ptkBoxedType = pCorLibTypeTokens->tkSystemInt32Type;
        break;
    case TypeCode::TYPE_CODE_INT64:
        *ptkBoxedType = pCorLibTypeTokens->tkSystemInt64Type;
        break;
    case TypeCode::TYPE_CODE_SBYTE:
        *ptkBoxedType = pCorLibTypeTokens->tkSystemSByteType;
        break;
    case TypeCode::TYPE_CODE_SINGLE:
        *ptkBoxedType = pCorLibTypeTokens->tkSystemSingleType;
        break;
    case TypeCode::TYPE_CODE_UINT16:
        *ptkBoxedType = pCorLibTypeTokens->tkSystemUInt16Type;
        break;
    case TypeCode::TYPE_CODE_UINT32:
        *ptkBoxedType = pCorLibTypeTokens->tkSystemUInt32Type;
        break;
    case TypeCode::TYPE_CODE_UINT64:
        *ptkBoxedType = pCorLibTypeTokens->tkSystemUInt64Type;
        break;

    case TypeCode::TYPE_CODE_EMPTY:
    case TypeCode::TYPE_CODE_DB_NULL:
    case TypeCode::TYPE_CODE_STRING:
    case TypeCode::TYPE_CODE_OBJECT:
    case TypeCode::TYPE_CODE_DATE_TIME:
        return E_FAIL;
    default:
        wprintf(L"using token: 0x%0x\n", (mdToken)typeInfo);
        *ptkBoxedType = (mdToken)typeInfo;
        break;
    }

    return S_OK;
}

HRESULT ProbeInjector::InstallProbe(
    ICorProfilerInfo* pICorProfilerInfo,
    ICorProfilerFunctionControl* pICorProfilerFunctionControl,
    struct InstrumentationRequest* pRequest,
    struct CorLibTypeTokens* pCorLibTypeTokens)
{
    HRESULT hr;
    ILRewriter rewriter(pICorProfilerInfo, pICorProfilerFunctionControl, pRequest->moduleId, pRequest->methodDef);

    IfFailRet(rewriter.Import());

    //
    // JSFIX: Wrap the probe in a try/catch.
    // In the catch PINVOKE into the profiler, notifying that a probe exception
    // occurred and the probes need to be uninstalled.
    //

    ILInstr* pInsertProbeBeforeThisInstr = rewriter.GetILList()->m_pNext;
    ILInstr * pNewInstr = nullptr;

    INT32 numArgs = (INT32)pRequest->tkBoxingTypes.size();

    /* Uniquifier: ModuleID + methodDef */
    pNewInstr = rewriter.NewILInstr();
    pNewInstr->m_opcode = CEE_LDC_I4;
    pNewInstr->m_Arg32 = (INT32)pRequest->moduleId;
    rewriter.InsertBefore(pInsertProbeBeforeThisInstr, pNewInstr);

    pNewInstr = rewriter.NewILInstr();
    pNewInstr->m_opcode = CEE_LDC_I4;
    pNewInstr->m_Arg32 = (INT32)pRequest->methodDef;
    rewriter.InsertBefore(pInsertProbeBeforeThisInstr, pNewInstr);

    /* Has This */
    pNewInstr = rewriter.NewILInstr();
    pNewInstr->m_opcode = CEE_LDC_I4;
    pNewInstr->m_Arg32 = (INT32)pRequest->hasThis;
    rewriter.InsertBefore(pInsertProbeBeforeThisInstr, pNewInstr);

    /* Args */

    // Size of array
    pNewInstr = rewriter.NewILInstr();
    pNewInstr->m_opcode = CEE_LDC_I4;
    pNewInstr->m_Arg32 = numArgs;
    rewriter.InsertBefore(pInsertProbeBeforeThisInstr, pNewInstr);

    // Create the array
    pNewInstr = rewriter.NewILInstr();
    pNewInstr->m_opcode = CEE_NEWARR;
    pNewInstr->m_Arg32 = pCorLibTypeTokens->tkSystemObjectType;
    rewriter.InsertBefore(pInsertProbeBeforeThisInstr, pNewInstr);

    for (INT32 i = 0; i < numArgs; i++)
    {
        // New entry on the evaluation stack
        pNewInstr = rewriter.NewILInstr();
        pNewInstr->m_opcode = CEE_DUP;
        rewriter.InsertBefore(pInsertProbeBeforeThisInstr, pNewInstr);

        // Index to set
        pNewInstr = rewriter.NewILInstr();
        pNewInstr->m_opcode = CEE_LDC_I4;
        pNewInstr->m_Arg32 = i;
        rewriter.InsertBefore(pInsertProbeBeforeThisInstr, pNewInstr);

        // Load arg
        UINT32 typeInfo = pRequest->tkBoxingTypes.at(i);
        if (typeInfo == (UINT)-1)
        {
            // JSFIX: Load a sentinel object/value provided by our managed layer instead of null.
            pNewInstr = rewriter.NewILInstr();
            pNewInstr->m_opcode = CEE_LDNULL;
            rewriter.InsertBefore(pInsertProbeBeforeThisInstr, pNewInstr);
        }
        else
        {
            pNewInstr = rewriter.NewILInstr();
            pNewInstr->m_opcode = CEE_LDARG_S; // JSFIX: Arglist support
            pNewInstr->m_Arg32 = i;
            rewriter.InsertBefore(pInsertProbeBeforeThisInstr, pNewInstr);

            if (typeInfo != TypeCode::TYPE_CODE_EMPTY)
            {
                // Resolve the box type
                mdToken tkBoxedType = mdTokenNil;
                IfFailRet(GetBoxingType(typeInfo, &tkBoxedType, pCorLibTypeTokens));
                wprintf(L"BOXING: 0x%0x\n", tkBoxedType);
                pNewInstr = rewriter.NewILInstr();
                pNewInstr->m_opcode = CEE_BOX;
                pNewInstr->m_Arg32 = tkBoxedType;
                rewriter.InsertBefore(pInsertProbeBeforeThisInstr, pNewInstr);
            }
        }

        // Replace the i'th element in our new array with what we just pushed on the stack
        pNewInstr = rewriter.NewILInstr();
        pNewInstr->m_opcode = CEE_STELEM_REF;
        rewriter.InsertBefore(pInsertProbeBeforeThisInstr, pNewInstr);
    }

    pNewInstr = rewriter.NewILInstr();
    pNewInstr->m_opcode = CEE_CALL;
    pNewInstr->m_Arg32 = pRequest->probeFunctionDef;
    rewriter.InsertBefore(pInsertProbeBeforeThisInstr, pNewInstr);

    IfFailRet(rewriter.Export());

    return S_OK;
}

HRESULT ProbeInjector::GetTypeToBoxWith(
    std::pair<CorElementType, mdToken> typeInfo,
    mdTypeDef* ptkBoxedType,
    struct CorLibTypeTokens* pCorLibTypeTokens)
{
    *ptkBoxedType = mdTypeDefNil;

    switch (typeInfo.first)
    {
    //
    // Types that don't require boxing
    //
    case ELEMENT_TYPE_ARRAY:
    case ELEMENT_TYPE_CLASS:
    case ELEMENT_TYPE_FNPTR:
    case ELEMENT_TYPE_OBJECT:
    case ELEMENT_TYPE_STRING:
    case ELEMENT_TYPE_SZARRAY:
        break;

    //
    // Well-known system types
    //
    case ELEMENT_TYPE_BOOLEAN:
        *ptkBoxedType = pCorLibTypeTokens->tkSystemBooleanType;
        break;
    case ELEMENT_TYPE_CHAR:
        *ptkBoxedType = pCorLibTypeTokens->tkSystemCharType;
    case ELEMENT_TYPE_I:
        *ptkBoxedType = pCorLibTypeTokens->tkSystemIntPtrType;
        break;
    case ELEMENT_TYPE_I1:
        *ptkBoxedType = pCorLibTypeTokens->tkSystemSByteType;
        break;
    case ELEMENT_TYPE_I2:
        *ptkBoxedType = pCorLibTypeTokens->tkSystemInt16Type;
        break;
    case ELEMENT_TYPE_I4:
        *ptkBoxedType = pCorLibTypeTokens->tkSystemInt32Type;
        break;
    case ELEMENT_TYPE_I8:
        *ptkBoxedType = pCorLibTypeTokens->tkSystemInt64Type;
        break;
    case ELEMENT_TYPE_R4:
        *ptkBoxedType = pCorLibTypeTokens->tkSystemSingleType;
        break;
    case ELEMENT_TYPE_R8:
        *ptkBoxedType = pCorLibTypeTokens->tkSystemDoubleType;
        break;
    case ELEMENT_TYPE_U:
        *ptkBoxedType = pCorLibTypeTokens->tkSystemUIntPtrType;
        break;
    case ELEMENT_TYPE_U1:
        *ptkBoxedType = pCorLibTypeTokens->tkSystemByteType;
        break;
    case ELEMENT_TYPE_U2:
        *ptkBoxedType = pCorLibTypeTokens->tkSystemUInt16Type;
        break;
    case ELEMENT_TYPE_U4:
        *ptkBoxedType = pCorLibTypeTokens->tkSystemUInt32Type;
        break;
    case ELEMENT_TYPE_U8:
        *ptkBoxedType = pCorLibTypeTokens->tkSystemUInt64Type;
        break;
    
    //
    // More complex scenarios
    //

    // case ELEMENT_TYPE_PTR:
    case PROBE_ELEMENT_TYPE_POINTER_LIKE_SENTINEL:
        // It's either a managed or native pointer, both of which are currently unsupported.
        break;

    case ELEMENT_TYPE_VALUETYPE:
        *ptkBoxedType = typeInfo.second;
        wprintf(L"using token: 0x%0x\n", (mdToken)typeInfo.second);
        break;

    //
    // JSFIX: Currently unsupported
    //
    case ELEMENT_TYPE_GENERICINST:
    case ELEMENT_TYPE_MVAR:
    case ELEMENT_TYPE_VAR:
        FEATURE_USAGE_GUARD();
        return E_FAIL;

    default:
        TEMPORARY_BREAK_ON_ERROR();
        return E_FAIL;
    }

    return S_OK;
}
