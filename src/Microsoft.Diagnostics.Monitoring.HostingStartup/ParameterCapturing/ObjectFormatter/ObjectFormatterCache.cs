// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using Microsoft.Extensions.Caching.Memory;
using System;
using System.Reflection;

namespace Microsoft.Diagnostics.Monitoring.HostingStartup.ParameterCapturing.ObjectFormatter
{
    internal sealed class ObjectFormatterCache : IObjectFormatterCache, IDisposable
    {
        private const int CacheSizeLimit = 1024;

        private readonly IMemoryCache _cache;
        private readonly bool _useDebuggerDisplayAttribute;

        public ObjectFormatterCache(bool useDebuggerDisplayAttribute)
        {
            _cache = new MemoryCache(new MemoryCacheOptions()
            {
                SizeLimit = CacheSizeLimit
            });
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
            if (_cache.TryGetValue(objType, out ObjectFormatter.Formatter formatter) && formatter != null)
            {
                return formatter;
            }

            ObjectFormatter.GeneratedFormatter generatedformatter = ObjectFormatter.GetFormatter(objType, _useDebuggerDisplayAttribute);
            foreach (Type type in generatedformatter.EncompassingTypes)
            {
                _cache.CreateEntry(type)
                    .SetValue(generatedformatter);
            }

            return generatedformatter.Formatter;
        }

        public void Dispose()
        {
            _cache.Dispose();
        }
    }
}
