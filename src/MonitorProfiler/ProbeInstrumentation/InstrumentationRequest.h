#pragma once

#include <corprof.h>
#include <corhdr.h>
#include <vector>

struct InstrumentationRequest
{
    mdToken probeFunctionDef;

    FunctionID functionId;
    std::vector<UINT32> tkBoxingTypes;

    ModuleID moduleId;
    mdMethodDef methodDef;

    BOOL hasThis;
    std::vector<std::pair<CorElementType, mdToken>> paramTypes;
};