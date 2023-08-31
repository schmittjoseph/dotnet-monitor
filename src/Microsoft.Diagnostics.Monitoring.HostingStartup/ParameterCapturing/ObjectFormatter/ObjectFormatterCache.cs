﻿// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using System;
using System.Collections.Concurrent;
using System.Diagnostics;
using System.Reflection;

namespace Microsoft.Diagnostics.Monitoring.HostingStartup.ParameterCapturing.ObjectFormatter
{
    [DebuggerDisplay("Count = {_cache.Count}, UseDebuggerDisplayAttribute={_useDebuggerDisplayAttribute}")]
    internal sealed class ObjectFormatterCache : IObjectFormatterCache
    {
        private readonly ConcurrentDictionary<Type, ObjectFormatter.Formatter> _cache = new();
        private readonly bool _useDebuggerDisplayAttribute;

        public ObjectFormatterCache(bool useDebuggerDisplayAttribute)
        {
            _useDebuggerDisplayAttribute = useDebuggerDisplayAttribute;
        }

        public void CacheMethodParameters(MethodInfo method)
        {
            if (method.HasImplicitThis() && method.DeclaringType != null)
            {
                _ = GetFormatter(method.DeclaringType);
            }

            ParameterInfo[] parameters = method.GetParameters();
            foreach (ParameterInfo parameter in parameters)
            {
                _ = GetFormatter(parameter.ParameterType);
            }
        }

        public ObjectFormatter.Formatter GetFormatter(Type objType)
        {
            if (_cache.TryGetValue(objType, out ObjectFormatter.Formatter? formatter) && formatter != null)
            {
                return formatter;
            }

            ObjectFormatter.GeneratedFormatter generatedFormatter = ObjectFormatter.GetFormatter(objType, _useDebuggerDisplayAttribute);
            foreach (Type type in generatedFormatter.EncompassingTypes)
            {
                _cache[type] = generatedFormatter.Formatter;
            }

            return generatedFormatter.Formatter;
        }
    }
}
