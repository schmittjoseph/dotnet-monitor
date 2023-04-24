// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using System;

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
            return true;
        }
        catch (Exception)
        {
            // Todo: Log
            return false;
        }
    }

    protected abstract void DoInit();

}
