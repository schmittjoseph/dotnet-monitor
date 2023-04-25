// Copyright (c) .NET Foundation and contributors. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#pragma once

#include "CorLibTypeTokens.h"

HRESULT InsertProbes(
    ICorProfilerInfo* pICorProfilerInfo,
    IMetaDataImport* pMetadataImport,
    IMetaDataEmit* pMetadataEmit,
    ICorProfilerFunctionControl* pICorProfilerFunctionControl,
    ModuleID moduleID,
    mdMethodDef methodDef,
    FunctionID functionId,
    mdMethodDef enterProbeDef,
    mdMethodDef leaveProbeDef,
    PCCOR_SIGNATURE sigParam,
    ULONG cbSigParam,
    struct CorLibTypeTokens* pCorLibTypeTokens);