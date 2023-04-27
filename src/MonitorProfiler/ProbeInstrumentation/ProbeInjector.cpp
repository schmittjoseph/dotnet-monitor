// Copyright (c) .NET Foundation and contributors. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "cor.h"
#include "corprof.h"
#include "ProbeInjector.h"
#include "ILRewriter.h"
#include <iostream>
#include <vector>

#include "JSFixUtils.h"

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

    INT32 numArgs = (INT32)pRequest->paramTypes.size();
    if (pRequest->hasThis)
    {
       numArgs++;
    }

    /* Func Id */
    pNewInstr = rewriter.NewILInstr();
    pNewInstr->m_opcode = CEE_LDC_I4;
    pNewInstr->m_Arg32 = (INT32)pRequest->functionId;
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

    INT32 typeIndex = (pRequest->hasThis) ? -1 : 0;
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
        pNewInstr = rewriter.NewILInstr();
        pNewInstr->m_opcode = CEE_LDARG_S; // JSFIX: Arglist support
        pNewInstr->m_Arg32 = i;
        rewriter.InsertBefore(pInsertProbeBeforeThisInstr, pNewInstr);

        // JSFIX: Check if there are any cases where "this" needs to be boxed.
        if (typeIndex >= 0)
        { 
            auto typeInfo = pRequest->paramTypes.at(typeIndex);

            mdTypeDef tkBoxedType = mdTypeDefNil;
            IfFailRet(GetTypeToBoxWith(typeInfo, &tkBoxedType, pCorLibTypeTokens));

            if (tkBoxedType != mdTypeDefNil)
            {
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

        typeIndex++;
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
    case ELEMENT_TYPE_VALUETYPE:
        *ptkBoxedType = typeInfo.second;
        break;

    //
    // JSFIX: Currently unsupported
    //
    case ELEMENT_TYPE_PTR:
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
