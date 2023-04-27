#pragma once

#include <corprof.h>
#include <corhdr.h>
#include <vector>

struct InstrumentationRequest
{
    mdMethodDef probeFunctionDef;

    FunctionID functionId;

    ModuleID moduleId;
    mdMethodDef methodDef;

    BOOL hasThis;
    std::vector<std::pair<CorElementType, mdToken>> paramTypes;
};