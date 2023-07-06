// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using System;
using System.Diagnostics;
using System.Globalization;
using System.Linq;
using System.Threading.Tasks;

namespace Microsoft.Diagnostics.Monitoring.Tool.ProcessReaper
{
    internal static class Program
    {
        static async Task<int> Main(string[] args)
        {
            if (args.Length < 2)
            {
                throw new InvalidOperationException("Missing args");
            }

            using Process parentProcess = Process.GetProcessById(int.Parse(args[0]));
            using Process currentProcess = Process.GetCurrentProcess();
            using Process childProcess = new();


            childProcess.StartInfo.UseShellExecute = false;
            childProcess.StartInfo.RedirectStandardError = true;
            childProcess.StartInfo.RedirectStandardInput = true;
            childProcess.StartInfo.RedirectStandardOutput = true;

            childProcess.StartInfo.FileName = currentProcess.StartInfo.FileName;
            foreach (string arg in args.Skip(1))
            {
                childProcess.StartInfo.ArgumentList.Add(arg);
            }

            foreach ((string key, string value) in childProcess.StartInfo.Environment)
            {
                string newKey = key;
                if (key.StartsWith(ProcessReaperIdentifiers.EnvironmentVariables.Passthrough, StringComparison.Ordinal))
                {
                    newKey = key[ProcessReaperIdentifiers.EnvironmentVariables.Passthrough.Length..];
                }

                childProcess.StartInfo.Environment[newKey] = value;
            }

            TaskCompletionSource<int> exitedSource = new(TaskCreationOptions.RunContinuationsAsynchronously);
            void exitedHandler(object s, EventArgs e) => exitedSource.SetResult(childProcess.ExitCode);

            childProcess.EnableRaisingEvents = true;
            childProcess.Exited += exitedHandler;
            childProcess.Start();
            try
            {
                await Console.Out.WriteLineAsync(Convert.ToString(childProcess.Id, CultureInfo.InvariantCulture));
            }
            catch
            {
                childProcess.Kill(entireProcessTree: true);
                throw;
            }

            await Task.WhenAny(exitedSource.Task, parentProcess.WaitForExitAsync());
            if (childProcess.HasExited)
            {
                return await exitedSource.Task;
            }
            else
            {
                childProcess.Kill(entireProcessTree: true);
                return 1;
            }
        }
    }
}
