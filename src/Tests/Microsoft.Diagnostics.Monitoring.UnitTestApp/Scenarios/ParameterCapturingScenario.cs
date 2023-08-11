// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Hosting;
using Microsoft.AspNetCore.Http;
using Microsoft.Diagnostics.Monitoring.TestCommon;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using System;
using System.CommandLine;
using System.Diagnostics;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using Xunit;

namespace Microsoft.Diagnostics.Monitoring.UnitTestApp.Scenarios
{
    internal sealed class ParameterCapturingScenario
    {
        public static CliCommand Command()
        {
            CliCommand command = new(TestAppScenarios.ParameterCapturing.Name);
            command.SetAction(ExecuteAsync);
            return command;
        }

        public static Task<int> ExecuteAsync(ParseResult result, CancellationToken token)
        {
            LogRecord logRecord = new();

            string[] acceptableCommands = new string[]
            {
                TestAppScenarios.ParameterCapturing.Commands.Continue,
                TestAppScenarios.ParameterCapturing.Commands.ExpectLogStatement,
                TestAppScenarios.ParameterCapturing.Commands.DoNotExpectLogStatement
            };

            return ScenarioHelpers.RunWebScenarioAsync<Startup>(
                configureServices: (services) =>
                {
                    services.AddLogging(builder =>
                    {
                        builder.AddProvider(new TestLoggerProvider(logRecord));
                    });
                },
                func: async logger =>
                {
                    while (!token.IsCancellationRequested)
                    {
                        string command = await ScenarioHelpers.WaitForCommandAsync(acceptableCommands, logger);

                        switch (command)
                        {
                            case TestAppScenarios.ParameterCapturing.Commands.ExpectLogStatement:
                                {
                                    while (!token.IsCancellationRequested &&
                                        !logRecord.Events.Where(e => e.Category == typeof(DotnetMonitor.ParameterCapture.Service).FullName).Any())
                                    {
                                        await Task.Delay(100).WaitAsync(token).ConfigureAwait(false);
                                    }
                                    token.ThrowIfCancellationRequested();

                                    SampleMethods.StaticTestMethodSignatures.Basic(Random.Shared.Next());
                                    LogRecordEntry logEntry = logRecord.Events.First(e => e.Category == typeof(DotnetMonitor.ParameterCapture.UserCode).FullName);
                                    Assert.NotNull(logEntry);
                                    break;
                                }

                            case TestAppScenarios.ParameterCapturing.Commands.DoNotExpectLogStatement:
                                {
                                    await Task.Delay(TimeSpan.FromSeconds(5));

                                    logRecord.Clear();
                                    SampleMethods.StaticTestMethodSignatures.Basic(Random.Shared.Next());

                                    bool didFindLogs = logRecord.Events.Where(e => e.Category == typeof(DotnetMonitor.ParameterCapture.UserCode).FullName).Any();
                                    Assert.False(didFindLogs);
                                    break;
                                }
                            case TestAppScenarios.ParameterCapturing.Commands.Continue:
                                return 0;
                        }
                    }

                    token.ThrowIfCancellationRequested();
                    return 0;
                }, token);
        }

        private sealed class Startup
        {
            public static void ConfigureServices(IServiceCollection services)
            {

                services.AddControllers();
            }

            public static void Configure(IApplicationBuilder app, IWebHostEnvironment env)
            {
                app.UseRouting();

                app.UseEndpoints(endpoints =>
                {
                    endpoints.MapGet("/", Responses.Ok);
                    endpoints.MapGet("/privacy", Responses.Ok);
                    endpoints.MapGet("/slowresponse", Responses.SlowResponseAsync);
                });
            }

            public static class Responses
            {
                public static IResult Ok() => Results.Ok();

                public static async Task<IResult> SlowResponseAsync()
                {
                    await Task.Delay(TimeSpan.FromMilliseconds(100));

                    return Results.Ok();
                }
            }
        }
    }
}
