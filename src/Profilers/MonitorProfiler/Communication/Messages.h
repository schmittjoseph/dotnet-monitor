// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

#pragma once

#include <vector>

enum class ProfilerCommand : unsigned short
{
    Callstack,

    // Indicate that any outstanding collection should be stopped and all data should be flushed
    // Currently a no-op
    StopAllFeatures,

    // Indicate that collection should resume again
    // Currently a no-op
    StartAllFeatures,
};

enum class StartupHookCommand : unsigned short
{
    StartCapturingParameters,
    StopCapturingParameters,

    // Indicates that all collection should stop
    // Request cancellation on all outstanding ParameterCapture requests
    // Unhooks from exception handling events, drains all the exception state, and resets the pipelines
    StopAllFeatures,

    // Indicates that all collection should start
    // Reconnects exception handler events
    StartAllFeatures,
};

enum class ServerResponseCommand : unsigned short
{
    Status
};

//
// Kept in sync with src\Microsoft.Diagnostics.Monitoring.WebApi\ProfilerMessage.cs even though not all
// command sets will be used by the profiler.
//
enum class CommandSet : unsigned short
{
    ServerResponse,
    Profiler,
    StartupHook
};

struct IpcMessage
{
    unsigned short CommandSet;
    unsigned short Command;
    std::vector<BYTE> Payload;
};
