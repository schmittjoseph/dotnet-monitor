#pragma once

#include "CorLibTypeTokens.h"

struct AssemblyProbeCacheEntry
{
    struct CorLibTypeTokens corLibTypeTokens;
    mdMemberRef tkProbeMemberRef;
};