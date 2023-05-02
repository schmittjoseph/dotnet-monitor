#pragma once

#include <corprof.h>
#include <corhdr.h>
#include <vector>

struct InstrumentationRequest
{
    FunctionID functionId;
    std::vector<UINT32> tkBoxingTypes;

    ModuleID moduleId;
    mdMethodDef methodDef;
};