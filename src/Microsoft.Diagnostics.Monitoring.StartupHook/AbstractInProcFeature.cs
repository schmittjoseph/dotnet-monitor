// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using Microsoft.Diagnostics.Monitoring.StartupHook;
using System;
using static Microsoft.Diagnostics.Monitoring.StartupHook.InProcLogger;

internal abstract class AbstractInProcFeature
{
    public AbstractInProcFeature()
    {

    }

    public bool TryInit()
    {
        try
        {
            DoInit();
            InProcLogger.Log($"Initialized {Name()}", LogLevel.Debug);
            return true;
        }
        catch (Exception ex)
        {
            InProcLogger.Log(ex.ToString(), LogLevel.Warning);
            return false;
        }
    }

    protected abstract string Name();

    protected abstract void DoInit();

}
