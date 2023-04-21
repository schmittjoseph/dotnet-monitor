// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using System;
using System.IO;
using System.Reflection;

namespace Microsoft.Diagnostics.Monitoring.StartupHook
{
    internal static class AspNetHostingStartupHelper
    {
        public const string SharedHostingStartupAssembly = "Microsoft.Diagnostics.Monitoring.HostingStartup";

        private const string HostingStartupEnvVariable = "ASPNETCORE_HOSTINGSTARTUPASSEMBLIES";

        //private const string SharedStoreEnvVariable = "DOTNET_SHARED_STORE";

        static Lazy<bool> DoRegisterAssemblyResolver = new Lazy<bool>(RegisterAssemblyResolver);

        // Not thread safe.
        public static void RegisterHostingStartup(string assembly = SharedHostingStartupAssembly)
        {
            if (!DoRegisterAssemblyResolver.Value)
            {
                return;
            }

            LoggerProxy.Log($"Registering hosting startup: {assembly}");
            AppendToEnvironmentVariable(HostingStartupEnvVariable, assembly);


            // Must also update the runtime store.
        }

        private static bool RegisterAssemblyResolver()
        {
            AppDomain.CurrentDomain.AssemblyResolve += AssemblyResolver!;

            return true;
        }


        static Assembly AssemblyResolver(object source, ResolveEventArgs e)
        {
            if (!e.Name.StartsWith(string.Concat(SharedHostingStartupAssembly, ","), StringComparison.Ordinal))
            {
                return null!;
            }

            Console.WriteLine("!! Intercepted request to load our hosting startup assembly, redirecting load !!");

            // We found it, unregister.
            AppDomain.CurrentDomain.AssemblyResolve -= AssemblyResolver!;
            //var path = JSFIX
            return Assembly.LoadFile(path);
        }

        private static void AppendToEnvironmentVariable(string key, string value, string delimiter = ";")
        {
            string? curValue = Environment.GetEnvironmentVariable(key);
            string newValue = string.IsNullOrWhiteSpace(curValue) ? value : $"{curValue}{delimiter}{value}";
            Environment.SetEnvironmentVariable(key, newValue);
        }
    }
}
