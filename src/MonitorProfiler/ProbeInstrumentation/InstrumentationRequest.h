#pragma once

#include <corprof.h>
#include <corhdr.h>
#include <vector>
#include <memory>

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

    std::shared_ptr<ASSEMBLY_PROBE_CACHE_ENTRY> assemblyProbeInformation;
} INSTRUMENTATION_REQUEST;

// JSFIX: Not worker anymore
typedef enum WorkerMessage
{
    INSTALL_PROBES,
    UNINSTALL_PROBES
} WorkerMessage;

typedef struct _WORKER_PAYLOAD
{
    WorkerMessage message;
    std::vector<UNPROCESSED_INSTRUMENTATION_REQUEST> requests;
} WORKER_PAYLOAD;