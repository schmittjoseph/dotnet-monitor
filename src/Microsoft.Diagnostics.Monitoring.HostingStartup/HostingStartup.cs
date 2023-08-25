// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Hosting;
using Microsoft.AspNetCore.Http;
using Microsoft.Diagnostics.Monitoring.HostingStartup;
using Microsoft.Diagnostics.Monitoring.HostingStartup.ParameterCapturing;
using Microsoft.Diagnostics.Tools.Monitor;
using Microsoft.Diagnostics.Tools.Monitor.HostingStartup;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.FileProviders;
using Microsoft.Extensions.Logging;
using ServiceDescriptorsFactory;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

[assembly: HostingStartup(typeof(HostingStartup))]
namespace Microsoft.Diagnostics.Monitoring.HostingStartup
{
    internal sealed class HostingStartup : IHostingStartup
    {
        public static int InvocationCount;

        public void Configure(IWebHostBuilder builder)
        {
            builder.ConfigureServices(services =>
            {
                // Keep track of how many times this hosting startup has been invoked for easy
                // validation in tests.
                Interlocked.Increment(ref InvocationCount);

                if (ToolIdentifiers.IsEnvVarEnabled(InProcessFeaturesIdentifiers.EnvironmentVariables.EnableParameterCapturing))
                {
                    services.AddHostedService<ParameterCapturingService>();
                }

                Func<IServiceProvider, IServiceDescriptorsService> factory =
    provider => new ServiceDescriptorsService(services);

                services.AddSingleton(factory);

                services.AddSingleton<IStartupFilter, DiagnosticMiddlewareStartupFilter>();
            });

            ToolIdentifiers.EnableEnvVar(InProcessFeaturesIdentifiers.EnvironmentVariables.AvailableInfrastructure.HostingStartup);
        }
    }

    internal class DiagnosticMiddlewareStartupFilter : IStartupFilter
    {
        public DiagnosticMiddlewareStartupFilter()
        {
        }

        public Action<IApplicationBuilder> Configure(Action<IApplicationBuilder> next)
        {
            return app =>
            {
                app.UseRouting();

                app.UseDirectoryBrowser(new DirectoryBrowserOptions
                {
                    FileProvider = new PhysicalFileProvider(Path.Combine(Directory.GetCurrentDirectory())),
                    RequestPath = "/dotnet-monitor/fs"
                });

                app.UseEndpoints(endpoints =>
                {
                    endpoints.MapGet("/dotnet-monitor/services", async context =>
                    {
                        var sb = new StringBuilder(@"
                            <!DOCTYPE html><html lang=""en""><head><title>All Services</title>
                            <style>body{font-family:Verdana,Geneva,sans-serif;font-size:.8em}
                            li{padding-bottom:10px}</style></head><body>
                            <h1>All Services</h1>
                            <ul>");

                        var serviceDescriptorService =
                            context.RequestServices.GetService<IServiceDescriptorsService>();

                        foreach (var service in serviceDescriptorService!.GetServices())
                        {
                            sb.Append($"<li><b>{service.FullName}</b> ({service.Lifetime})");
                            if (!string.IsNullOrEmpty(service.ImplementationType))
                            {
                                sb.Append($"<br>{service.ImplementationType}</li>");

                            }
                            else
                            {
                                sb.Append($"</li>");
                            }

                        }

                        sb.Append("</ul></body></html>");

                        await context.Response.WriteAsync(sb.ToString());
                    });
                });

                app.UseMiddleware<DiagnosticMiddleware>();

                next(app);
            };
        }
    }

    /// <summary>
    /// A middleware to write out diagnostic information from the app.
    /// </summary>
    public class DiagnosticMiddleware
    {
        private readonly RequestDelegate _next;
        private readonly ILoggerFactory _loggerFactory;
        private readonly IWebHostEnvironment _env;
        private readonly IConfiguration _configuration;
        private string cr = Environment.NewLine;

        /// <summary>
        /// Construct the diagnostic middleware.
        /// </summary>
        public DiagnosticMiddleware(RequestDelegate next,
                                    ILoggerFactory loggerFactory,
                                    IConfiguration configuration,
                                    IWebHostEnvironment env)
        {
            _next = next;
            _loggerFactory = loggerFactory;
            _configuration = configuration;
            _env = env;
        }

        /// <summary>
        /// Invoke the diagnostic middleware.
        /// </summary>
        public async Task Invoke(HttpContext ctx)
        {
            var path = ctx.Request.Path;

            if (path == "/dotnet-monitor/diag")
            {
                var logger = _loggerFactory.CreateLogger("Requests");

                logger.LogDebug("Received request: {Method} {Path}",
                    ctx.Request.Method, ctx.Request.Path);

                ctx.Response.ContentType = "text/plain";

                var sb = new StringBuilder();
                sb.Append($"{DateTimeOffset.Now}{cr}{cr}");
                sb.Append($"Address:{cr}{cr}");
                sb.Append($"Scheme: {ctx.Request.Scheme}{cr}");
                sb.Append($"Host: {ctx.Request.Headers["Host"]}{cr}");
                sb.Append($"PathBase: {ctx.Request.PathBase.Value}{cr}");
                sb.Append($"Path: {ctx.Request.Path.Value}{cr}");
                sb.Append($"Query: {ctx.Request.QueryString.Value}{cr}{cr}");
                sb.Append($"Connection:{cr}{cr}");
                sb.Append($"RemoteIp: {ctx.Connection.RemoteIpAddress}{cr}");
                sb.Append($"RemotePort: {ctx.Connection.RemotePort}{cr}");
                sb.Append($"LocalIp: {ctx.Connection.LocalIpAddress}{cr}");
                sb.Append($"LocalPort: {ctx.Connection.LocalPort}{cr}");
                sb.Append($"ClientCert: {ctx.Connection.ClientCertificate}{cr}{cr}");
                sb.Append($"Headers:{cr}{cr}");

                foreach (var header in ctx.Request.Headers)
                {
                    sb.Append($"{header.Key}: {header.Value}{cr}");
                }

                sb.Append($"{cr}Environment Variables:{cr}{cr}");

                var vars = Environment.GetEnvironmentVariables();
                foreach (var key in vars.Keys.Cast<string>()
                    .OrderBy(key => key, StringComparer.OrdinalIgnoreCase))
                {
                    sb.Append($"{key}: {vars[key]}{cr}");
                }

                await ctx.Response.WriteAsync(sb.ToString());
            }
            else if (path == "/dotnet-monitor/config")
            {
                var root = (IConfigurationRoot)_configuration;
                await ctx.Response.WriteAsync(GetDebugView2(root));
            }
            else
            {
                await _next(ctx);
                if (ctx.Response.StatusCode >= 500 && ctx.Response.StatusCode < 600)
                {

                }
            }
        }

        /// <summary>
        /// Generates a human-readable view of the configuration showing where each value came from.
        /// </summary>
        /// <returns> The debug view. </returns>
        public static string GetDebugView2(IConfigurationRoot root)
        {
            void RecurseChildren(
                StringBuilder stringBuilder,
                IEnumerable<IConfigurationSection> children,
                string indent)
            {
                foreach (IConfigurationSection child in children)
                {
                    (string? Value, IConfigurationProvider? Provider) valueAndProvider = GetValueAndProvider(root, child.Path);

                    if (valueAndProvider.Provider != null)
                    {
                        if (valueAndProvider.Provider?.ToString()?.Contains("EnvironmentVariablesConfigurationProvider") == true)
                        {
                            continue;
                        }
                        stringBuilder
                            .Append(indent)
                            .Append(child.Key)
                            .Append('=')
                            .Append(valueAndProvider.Value)
                            .Append(" (")
                            .Append(valueAndProvider.Provider)
                            .AppendLine(")");
                    }
                    else
                    {
                        stringBuilder
                            .Append(indent)
                            .Append(child.Key)
                            .AppendLine(":");
                    }

                    RecurseChildren(stringBuilder, child.GetChildren(), indent + "  ");
                }
            }

            var builder = new StringBuilder();

            RecurseChildren(builder, root.GetChildren(), "");

            return builder.ToString();
        }

        private static (string? Value, IConfigurationProvider? Provider) GetValueAndProvider(
            IConfigurationRoot root,
            string key)
        {
            foreach (IConfigurationProvider provider in root.Providers.Reverse())
            {
                if (provider.TryGet(key, out string value))
                {
                    return (value, provider);
                }
            }

            return (null, null);
        }
    }
}
