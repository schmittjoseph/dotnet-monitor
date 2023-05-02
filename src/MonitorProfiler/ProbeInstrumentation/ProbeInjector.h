// Copyright (c) .NET Foundation and contributors. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#pragma once

#include "CorLibTypeTokens.h"
#include "InstrumentationRequest.h"
#include <vector>

class ProbeInjector
{
    public:
        static HRESULT InstallProbe(
            ICorProfilerInfo* pICorProfilerInfo,
            ICorProfilerFunctionControl* pICorProfilerFunctionControl,
            struct InstrumentationRequest* pRequest);

    private:       
        static HRESULT GetBoxingType(
            UINT32 typeInfo,
            mdToken* ptkBoxedType,
            struct CorLibTypeTokens* pCorLibTypeTokens);
};