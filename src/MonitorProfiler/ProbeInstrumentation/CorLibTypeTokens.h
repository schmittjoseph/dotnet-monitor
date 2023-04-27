#pragma once

// JSFIX: Hacky -- pick a better way of signalling this.
#define PROBE_ELEMENT_TYPE_POINTER_LIKE_SENTINEL ELEMENT_TYPE_PTR

struct CorLibTypeTokens
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
};