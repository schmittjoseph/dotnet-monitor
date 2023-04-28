// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using System;
using System.IO;
using System.Reflection;

namespace Microsoft.Diagnostics.Monitoring.StartupHook
{
    internal static class AspNetHostingStartupHelper
    {
        private const string HostingStartupEnvVariable = "ASPNETCORE_HOSTINGSTARTUPASSEMBLIES";
        //private const string DotnetMonitorStartupGuid = "30CB586D-62C1-4364-B172-5042CA6EF1D1";
        private const string DotnetMonitorStartupGuid = "Microsoft.Diagnostics.Monitoring.HostingStartup";
        static Lazy<bool> DoRegisterAssemblyResolver = new Lazy<bool>(RegisterAssemblyResolver);


        // Not thread safe.
        public static void RegisterHostingStartup()
        {
            if (!DoRegisterAssemblyResolver.Value)
            {
                return;
            }
        }

        private static bool RegisterAssemblyResolver()
        {
            AppDomain.CurrentDomain.AssemblyResolve += AssemblyResolver!;
            AppendToEnvironmentVariable(HostingStartupEnvVariable, DotnetMonitorStartupGuid);
            return true;
        }

        private static Assembly? AssemblyResolver(object source, ResolveEventArgs e)
        {
            if (!e.Name.StartsWith(DotnetMonitorStartupGuid, StringComparison.Ordinal))
            {
                return null;
            }

            // Can check that e.EquestingAssembly == "Microsoft.AspNetCore.Hosting"
            // Can also use this to determine the aspnet version
            // Microsoft.AspNetCore.Hosting, Version=7.0.0.0, Culture=neutral, PublicKeyToken=adb9793829ddae60

            // A little hacky, but we'll be asked to resolve this dll *twice*.
            // Once for aspnetcore's hosting startup logic.
            // Once for first-time managed probe resolution.

            //AppDomain.CurrentDomain.AssemblyResolve -= AssemblyResolver!;
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
