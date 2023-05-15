// Copyright (c) .NET Foundation and contributors. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "cor.h"
#include "corprof.h"
#include "ProbeInjector.h"
#include "../Utilities/ILRewriter.h"
#include <vector>

const UINT32 typeBoxingType = 0xff000000;
typedef enum BoxingType
{
    TYPE_UNKNOWN = 0x00,
    TYPE_OBJECT  = 0x01,
    TYPE_BOOLEAN = 0x02,
    TYPE_CHAR    = 0x03,
    TYPE_SBYTE   = 0x04,
    TYPE_BYTE    = 0x05,
    TYPE_INT16   = 0x06,
    TYPE_UINT16  = 0x07,
    TYPE_INT32   = 0x08,
    TYPE_UINT32  = 0x09,
    TYPE_INT64   = 0x0a,
    TYPE_UINT64  = 0x0b,
    TYPE_SINGLE  = 0x0c,
    TYPE_DOUBLE  = 0x0d
} BoxingType;

HRESULT ProbeInjector::InstallProbe(
    ICorProfilerInfo* pICorProfilerInfo,
    ICorProfilerFunctionControl* pICorProfilerFunctionControl,
    const INSTRUMENTATION_REQUEST& request)
{
    IfNullRet(pICorProfilerInfo);
    IfNullRet(pICorProfilerFunctionControl);

    HRESULT hr;
    ILRewriter rewriter(pICorProfilerInfo, pICorProfilerFunctionControl, request.moduleId, request.methodDef);
    IfFailRet(rewriter.Import());

    const COR_LIB_TYPE_TOKENS corLibTypeTokens = request.pAssemblyData->GetCorLibTypeTokens();

    //
    // JSFIX: Wrap the probe in a try/catch.
    // In the catch PINVOKE into the profiler, notifying that a probe exception
    // occurred and the probes need to be uninstalled.
    //

    ILInstr* pInsertProbeBeforeThisInstr = rewriter.GetILList()->m_pNext;
    ILInstr* pNewInstr = nullptr;

    INT32 numArgs = (INT32)request.tkBoxingTypes.size();

    /* uniquifier */
    pNewInstr = rewriter.NewILInstr();
    pNewInstr->m_opcode = CEE_LDC_I8;
    pNewInstr->m_Arg64 = request.uniquifier;
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
    pNewInstr->m_Arg32 = corLibTypeTokens.tkSystemObjectType;
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
        UINT32 typeInfo = request.tkBoxingTypes.at(i);
        if (typeInfo == (typeBoxingType | BoxingType::TYPE_UNKNOWN))
        {
            pNewInstr = rewriter.NewILInstr();
            pNewInstr->m_opcode = CEE_LDNULL;
            rewriter.InsertBefore(pInsertProbeBeforeThisInstr, pNewInstr);
        }
        else
        {
            pNewInstr = rewriter.NewILInstr();
            pNewInstr->m_opcode = CEE_LDARG_S;
            pNewInstr->m_Arg32 = i;
            rewriter.InsertBefore(pInsertProbeBeforeThisInstr, pNewInstr);

            // Resolve the box type
            mdToken boxedTypeToken = mdTokenNil;
            IfFailRet(GetBoxingToken(typeInfo, corLibTypeTokens, boxedTypeToken));
            if (boxedTypeToken != mdTokenNil)
            {
                pNewInstr = rewriter.NewILInstr();
                pNewInstr->m_opcode = CEE_BOX;
                pNewInstr->m_Arg32 = boxedTypeToken;
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
    pNewInstr->m_Arg32 = request.pAssemblyData->GetProbeMemberRef();
    rewriter.InsertBefore(pInsertProbeBeforeThisInstr, pNewInstr);

    IfFailRet(rewriter.Export());

    return S_OK;
}

HRESULT ProbeInjector::GetBoxingToken(
    UINT32 typeInfo,
    const COR_LIB_TYPE_TOKENS& corLibTypeTokens,
    mdToken& boxedType)
{
    boxedType = mdTokenNil;

    if (TypeFromToken(typeInfo) != typeBoxingType)
    {
        boxedType = static_cast<mdToken>(typeInfo);
        return S_OK;
    }

    switch(static_cast<BoxingType>(RidFromToken(typeInfo)))
    {
    case BoxingType::TYPE_BOOLEAN:
        boxedType = corLibTypeTokens.tkSystemBooleanType;
        break;
    case BoxingType::TYPE_BYTE:
        boxedType = corLibTypeTokens.tkSystemByteType;
        break;
    case BoxingType::TYPE_CHAR:
        boxedType = corLibTypeTokens.tkSystemCharType;
        break;
    case BoxingType::TYPE_DOUBLE:
        boxedType = corLibTypeTokens.tkSystemDoubleType;
        break;
    case BoxingType::TYPE_INT16:
        boxedType = corLibTypeTokens.tkSystemInt16Type;
        break;
    case BoxingType::TYPE_INT32:
        boxedType = corLibTypeTokens.tkSystemInt32Type;
        break;
    case BoxingType::TYPE_INT64:
        boxedType = corLibTypeTokens.tkSystemInt64Type;
        break;
    case BoxingType::TYPE_SBYTE:
        boxedType = corLibTypeTokens.tkSystemSByteType;
        break;
    case BoxingType::TYPE_SINGLE:
        boxedType = corLibTypeTokens.tkSystemSingleType;
        break;
    case BoxingType::TYPE_UINT16:
        boxedType = corLibTypeTokens.tkSystemUInt16Type;
        break;
    case BoxingType::TYPE_UINT32:
        boxedType = corLibTypeTokens.tkSystemUInt32Type;
        break;
    case BoxingType::TYPE_UINT64:
        boxedType = corLibTypeTokens.tkSystemUInt64Type;
        break;

    case BoxingType::TYPE_OBJECT:
        // No boxing needed.
        break;

    case BoxingType::TYPE_UNKNOWN:
    default:
        return E_FAIL;
    }

    return S_OK;
}