#pragma once

#include <corprof.h>
#include <corhdr.h>
#include <vector>
#include <memory>
#include "AssemblyProbePrep.h"
#include "tstring.h"

typedef struct _UNPROCESSED_INSTRUMENTATION_REQUEST
{
    FunctionID functionId;
    std::vector<ULONG32> tkBoxingTypes;
} UNPROCESSED_INSTRUMENTATION_REQUEST;

typedef struct _INSTRUMENTATION_REQUEST
{
    UINT64 uniquifier;
    std::vector<ULONG32> tkBoxingTypes;

    ModuleID moduleId;
    mdMethodDef methodDef;

    std::shared_ptr<AssemblyProbePrepData> pAssemblyData;
} INSTRUMENTATION_REQUEST;
