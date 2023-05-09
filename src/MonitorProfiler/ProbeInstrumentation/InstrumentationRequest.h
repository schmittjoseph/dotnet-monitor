#pragma once

#include <corprof.h>
#include <corhdr.h>
#include <vector>

typedef struct _COR_LIB_TYPE_TOKENS
{
    mdToken
        tkSystemBooleanType,
        tkSystemByteType,
        tkSystemCharType,
        tkSystemDoubleType,
        tkSystemInt16Type,
        tkSystemInt32Type,
        tkSystemInt64Type,
        tkSystemObjectType,
        tkSystemSByteType,
        tkSystemSingleType,
        tkSystemStringType,
        tkSystemUInt16Type,
        tkSystemUInt32Type,
        tkSystemUInt64Type,
        tkSystemIntPtrType,
        tkSystemUIntPtrType;
} COR_LIB_TYPE_TOKENS;

typedef struct _ASSEMBLY_PROBE_CACHE_ENTRY
{
    COR_LIB_TYPE_TOKENS corLibTypeTokens;
    mdMemberRef tkProbeMemberRef;
} ASSEMBLY_PROBE_CACHE_ENTRY;

typedef struct _INSTRUMENTATION_REQUEST
{
    FunctionID functionId;
    std::vector<UINT32> tkBoxingTypes;

    ModuleID moduleId;
    mdMethodDef methodDef;

    ASSEMBLY_PROBE_CACHE_ENTRY* pAssemblyProbeInformation;
} INSTRUMENTATION_REQUEST;