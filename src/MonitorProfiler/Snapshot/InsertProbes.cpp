// Copyright (c) .NET Foundation and contributors. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "cor.h"
#include "corprof.h"
#include "InsertProbes.h"
#include "ILRewriter.h"

HRESULT GetOneElementType(PCCOR_SIGNATURE pbSigBlob, ULONG ulSigBlob, ULONG *pcb, CorElementType* elementType)
{
    HRESULT hr = S_OK;
    ULONG cb;
    ULONG cbCur = 0;
    ULONG ulData;

    cb = CorSigUncompressData(pbSigBlob, &ulData);
    if (cb == ULONG(-1)) {
        return E_FAIL;
    }

    cbCur += cb;

    // Handle the modifiers.
    if (ulData & ELEMENT_TYPE_MODIFIER)
    {
        if (ulData == ELEMENT_TYPE_SENTINEL) {

        } else if (ulData == ELEMENT_TYPE_PINNED) {

        } else {
            return E_FAIL;
        }
        IfFailRet(GetOneElementType(&pbSigBlob[cbCur], ulSigBlob-cbCur, &cb, elementType));

        cbCur += cb;
        if (cbCur > ulSigBlob) {
            return E_FAIL;
        } else {
            return S_OK;
        }
    }

    // Handle the underlying element types.
    if (ulData >= ELEMENT_TYPE_MAX) 
    {
        return E_FAIL;
    }
    while (ulData == ELEMENT_TYPE_PTR || ulData == ELEMENT_TYPE_BYREF)
    {
        cb = CorSigUncompressData(&pbSigBlob[cbCur], &ulData);
        cbCur += cb;
    }

    // Generics
    if (ulData == ELEMENT_TYPE_VAR)
    {
        // The next byte represents which generic parameter is referred to.  We
        // do not currently use this information, so just bypass this byte.
        cbCur++;
        if (cbCur > ulSigBlob) {
            return E_FAIL;
        } else {
            return S_OK;
        }
    }

    // A generic instance, e.g. IEnumerable<String>
    // JSFIX: It looks like we might be clobbering cb before looking at generic params
    if (ulData == ELEMENT_TYPE_GENERICINST)
    {
        // Print out the base type.
        IfFailRet(GetOneElementType(&pbSigBlob[cbCur], ulSigBlob-cbCur, &cb, elementType));
        cbCur += cb;

        // Get the number of generic arguments.
        ULONG numParams = 0;
        IfFailRet(CorSigUncompressData(&pbSigBlob[cbCur], 1, &numParams, &cb));
        cbCur += cb;

        // Skip past the arguments
        for (ULONG i = 0; i < numParams; i++)
        {
            IfFailRet(GetOneElementType(&pbSigBlob[cbCur], ulSigBlob-cbCur, &cb, NULL));
            cbCur += cb;
        }
        if (cbCur > ulSigBlob) {
            return E_FAIL;
        } else {
            return S_OK;
        }
    }

    if (elementType != NULL) {
        *elementType = static_cast<CorElementType>(ulData);
    }

    return S_OK;
}

HRESULT ProcessArgs(PCCOR_SIGNATURE pbSigBlob, ULONG ulSigBlob, BOOL* hasThis, CorElementType* elementTypes, INT32* numArgs)
{
    ULONG       cbCur = 0;
    ULONG       cb;
    ULONG       ulData = NULL;
    ULONG       ulArgs;
    HRESULT     hr = S_OK;

    *numArgs = 0;
    *hasThis = FALSE;

    cb = CorSigUncompressData(pbSigBlob, &ulData);

    if (cb > ulSigBlob) {
        return E_FAIL;
    }
    cbCur += cb;
    ulSigBlob -= cb;

    if (ulData & IMAGE_CEE_CS_CALLCONV_HASTHIS) {
        *hasThis = TRUE;
    }
    if (ulData & IMAGE_CEE_CS_CALLCONV_EXPLICITTHIS) {
        *hasThis = TRUE;
    }

    if (isCallConv(ulData, IMAGE_CEE_CS_CALLCONV_FIELD)) {
        // Do nothing
        return S_OK;
    }

    cb = CorSigUncompressData(&pbSigBlob[cbCur], &ulArgs);
    *numArgs = ulArgs; // JSFIX: vargs

    if (cb > ulSigBlob) {

        return E_FAIL;
    }
    cbCur += cb;
    ulSigBlob -= cb;

    if (ulData != IMAGE_CEE_CS_CALLCONV_LOCAL_SIG)
    {
        // Return type.
        IfFailRet(GetOneElementType(&pbSigBlob[cbCur], ulSigBlob, &cb, NULL)); // JSFIX;

        if (cb > ulSigBlob) {
            return E_FAIL;
        }

        cbCur += cb;
        ulSigBlob -= cb;
    }

    ULONG i = 0;
    while (i < ulArgs && ulSigBlob > 0)
    {
        ULONG ulDataUncompress;

        // Handle the sentinel for varargs because it isn't counted in the args.
        CorSigUncompressData(&pbSigBlob[cbCur], &ulDataUncompress);
    
        CorElementType elementType = ELEMENT_TYPE_END;
        IfFailRet(GetOneElementType(&pbSigBlob[cbCur], ulSigBlob, &cb, &elementType));

        if (cb > ulSigBlob) {
            return E_FAIL;
        }

        elementTypes[i] = elementType;

        cbCur += cb;
        ulSigBlob -= cb;
        i++;
    }

    cb = 0;

    return S_OK;
}

HRESULT GetTypeToBoxWith(CorElementType elementType, mdTypeDef* ptkBoxedType, struct CorLibTypeTokens * pCorLibTypeTokens) {
    HRESULT hr = S_OK;
    *ptkBoxedType = mdTypeDefNil;

    // JSFIX: Support more complex elements
    switch (elementType)
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
    default:
        return E_FAIL;
    }

    return S_OK;
}


HRESULT AddProbe(
    ILRewriter * pilr,
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

    CorElementType argTypes[16];
    IfFailRet(ProcessArgs(sigParam, cbSigParam, &hasThis, argTypes, &numArgs));

    if (hasThis) {
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
    for (INT32 i = 0; i < numArgs; i++) {
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

        // Check if it's a Value Type, if so we box.
        if (typeIndex >= 0) { // JSFIX: Validate -- this never needs to be boxed?
            mdTypeDef tkBoxedType = mdTypeDefNil;
            IfFailRet(GetTypeToBoxWith(argTypes[typeIndex], &tkBoxedType, pCorLibTypeTokens));

            if (tkBoxedType != mdTypeDefNil) {
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

HRESULT AddExitProbe(
    ILRewriter * pilr,
    FunctionID functionId,
    mdMethodDef probeFunctionDef,
    mdTypeDef sysObjectTypeDef,
    ILInstr * pInsertProbeBeforeThisInstr)
{
    ILInstr * pNewInstr = nullptr;

    /* Func Id */
    pNewInstr = pilr->NewILInstr();
    pNewInstr->m_opcode = CEE_LDC_I4;
    pNewInstr->m_Arg32 = (INT32)functionId;
    pilr->InsertBefore(pInsertProbeBeforeThisInstr, pNewInstr);

    pNewInstr = pilr->NewILInstr();
    pNewInstr->m_opcode = CEE_CALL;
    pNewInstr->m_Arg32 = probeFunctionDef;
    pilr->InsertBefore(pInsertProbeBeforeThisInstr, pNewInstr);

    return S_OK;
}


HRESULT AddEnterProbe(
    ILRewriter * pilr,
    FunctionID functionId,
    mdMethodDef probeFunctionDef,
    PCCOR_SIGNATURE sigParam,
    ULONG cbSigParam,
    struct CorLibTypeTokens * pCorLibTypeTokens)
{
    ILInstr * pFirstOriginalInstr = pilr->GetILList()->m_pNext;

    return AddProbe(pilr, functionId, probeFunctionDef, pFirstOriginalInstr, sigParam, cbSigParam, pCorLibTypeTokens);
}


HRESULT AddExitProbe(
    ILRewriter * pilr,
    FunctionID functionId,
    mdMethodDef probeFunctionDef)
{
    HRESULT hr;
    BOOL fAtLeastOneProbeAdded = FALSE;

    // Find all RETs, and insert a call to the exit probe before each one.
    for (ILInstr * pInstr = pilr->GetILList()->m_pNext; pInstr != pilr->GetILList(); pInstr = pInstr->m_pNext)
    {
        switch (pInstr->m_opcode)
        {
        case CEE_RET:
        {
            // We want any branches or leaves that targeted the RET instruction to
            // actually target the epilog instructions we're adding. So turn the "RET"
            // into ["NOP", "RET"], and THEN add the epilog between the NOP & RET. That
            // ensures that any branches that went to the RET will now go to the NOP and
            // then execute our epilog.

            // NOTE: The NOP is not strictly required, but is a simplification of the implementation.
            // RET->NOP
            pInstr->m_opcode = CEE_NOP;

            // Add the new RET after
            ILInstr * pNewRet = pilr->NewILInstr();
            pNewRet->m_opcode = CEE_RET;
            pilr->InsertAfter(pInstr, pNewRet);

            // Add now insert the epilog before the new RET
            hr = AddExitProbe(pilr, functionId, probeFunctionDef, mdTokenNil, pNewRet);
            if (FAILED(hr))
                return hr;
            fAtLeastOneProbeAdded = TRUE;

            // Advance pInstr after all this gunk so the for loop continues properly
            pInstr = pNewRet;
            break;
        }

        default:
            break;
        }
    }

    if (!fAtLeastOneProbeAdded)
        return E_FAIL;

    return S_OK;
}

HRESULT InsertProbes(
    ICorProfilerInfo * pICorProfilerInfo,
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
    {
        // Adds enter/exit probes
        IfFailRet(AddEnterProbe(&rewriter, functionId, enterProbeDef, sigParam, cbSigParam, pCorLibTypeTokens));
        // JSFIX: Re-enable probe.
        //IfFailRet(AddExitProbe(&rewriter, functionId, leaveProbeDef));
    }
    IfFailRet(rewriter.Export());

    return S_OK;
}