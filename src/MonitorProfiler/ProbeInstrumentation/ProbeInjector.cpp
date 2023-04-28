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
            // JSFIX: No argslist or indirect load support.
            pNewInstr = rewriter.NewILInstr();
            pNewInstr->m_opcode = CEE_LDARG_S;
            pNewInstr->m_Arg32 = i;
            rewriter.InsertBefore(pInsertProbeBeforeThisInstr, pNewInstr);

            if (typeInfo != TypeCode::TYPE_CODE_EMPTY)
            {
                // Resolve the box type
                mdToken tkBoxedType = mdTokenNil;
                IfFailRet(GetBoxingType(typeInfo, &tkBoxedType, pCorLibTypeTokens));
                if (tkBoxedType != mdTokenNil)
                {
                    pNewInstr = rewriter.NewILInstr();
                    pNewInstr->m_opcode = CEE_BOX;
                    pNewInstr->m_Arg32 = tkBoxedType;
                    rewriter.InsertBefore(pInsertProbeBeforeThisInstr, pNewInstr);
                }
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

HRESULT ProbeInjector::GetBoxingType(
    UINT32 typeInfo,
    mdToken* ptkBoxedType,
    struct CorLibTypeTokens* pCorLibTypeTokens)
{
    *ptkBoxedType = mdTokenNil;

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

    case TypeCode::TYPE_CODE_OBJECT:
        // No boxing needed.
        break;

    case TypeCode::TYPE_CODE_EMPTY:
    case TypeCode::TYPE_CODE_DB_NULL:
    case TypeCode::TYPE_CODE_STRING:
    case TypeCode::TYPE_CODE_DATE_TIME:
        TEMPORARY_BREAK_ON_ERROR();
        return E_FAIL;
    default:
        wprintf(L"using token: 0x%0x\n", (mdToken)typeInfo);
        *ptkBoxedType = (mdToken)typeInfo;
        break;
    }

    return S_OK;
}