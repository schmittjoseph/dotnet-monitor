#pragma once

#include <corprof.h>
#include <corhdr.h>
#include <vector>
#include "AssemblyProbeCacheEntry.h"

struct InstrumentationRequest
{
    FunctionID functionId;
    std::vector<UINT32> tkBoxingTypes;

    ModuleID moduleId;
    mdMethodDef methodDef;

    struct AssemblyProbeCacheEntry* pAssemblyProbeInformation;
};