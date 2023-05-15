// Copyright (c) .NET Foundation and contributors. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#pragma once

#include "InstrumentationRequest.h"
#include "AssemblyProbePrep.h"
#include <vector>

class ProbeInjector
{
    public:
        static HRESULT InstallProbe(
            ICorProfilerInfo* pICorProfilerInfo,
            ICorProfilerFunctionControl* pICorProfilerFunctionControl,
            INSTRUMENTATION_REQUEST* pRequest);

    private:       
        static HRESULT GetBoxingToken(
            UINT32 typeInfo,
            mdToken* ptkBoxedType,
            const COR_LIB_TYPE_TOKENS pCorLibTypeTokens);
};