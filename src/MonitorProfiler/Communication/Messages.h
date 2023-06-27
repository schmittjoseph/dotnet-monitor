// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

#pragma once

#include <vector>

enum class PayloadType : short
{
    None,
    Int32Parameter,
    Utf8Json
};

enum class MessageType : short
{
    Unknown,
    Status,
    Callstack
};

struct IpcMessage
{
    PayloadType PayloadType = PayloadType::None;
    MessageType MessageType = MessageType::Unknown;
    std::vector<BYTE> Payload;
};

struct Int32ParameterIpcMessage
{
    PayloadType PayloadType = PayloadType::Int32Parameter;
    MessageType MessageType = MessageType::Unknown;
    int Parameters;
};
