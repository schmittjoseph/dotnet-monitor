#pragma once

// JSFIX: Remove
#ifdef DEBUG
#define FEATURE_USAGE_GUARD() __debugbreak()
#define TEMPORARY_BREAK_ON_ERROR() __debugbreak()
#else
#define FEATURE_USAGE_GUARD() 
#define TEMPORARY_BREAK_ON_ERROR()
#endif