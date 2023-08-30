// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using System;
using System.Reflection;

namespace Microsoft.Diagnostics.Monitoring.HostingStartup.ParameterCapturing.ObjectFormatter
{
    internal interface IObjectFormatterCache
    {
        public void CacheMethodParameters(MethodInfo method);
        public ObjectFormatter.Formatter GetFormatter(Type objType);
    }
}
