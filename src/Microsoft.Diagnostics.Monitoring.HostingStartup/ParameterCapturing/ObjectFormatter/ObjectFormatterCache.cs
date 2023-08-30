// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using Microsoft.Extensions.Caching.Memory;
using System;

namespace Microsoft.Diagnostics.Monitoring.HostingStartup.ParameterCapturing.ObjectFormatter
{
    internal sealed class ObjectFormatterCache : IDisposable
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

        public ObjectFormatter.Formatter GetFormatter(Type objType)
        {
            if (_cache.TryGetValue(objType, out ObjectFormatter.Formatter formatter))
            {
                return formatter;
            }

            ObjectFormatter.GeneratedFormatter generatedformatter = ObjectFormatter.GetFormatter(objType, _useDebuggerDisplayAttribute);
            foreach (Type type in generatedformatter.EncompassingTypes)
            {
                _cache.CreateEntry(type).Value = generatedformatter.Formatter;
            }

            return generatedformatter.Formatter;
        }

        public void Dispose()
        {
            _cache.Dispose();
        }
    }
}
