// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using Microsoft.Diagnostics.Monitoring.WebApi.Models;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;

namespace Microsoft.Diagnostics.Monitoring.HostingStartup.ParameterCapturing
{
    public class UnresolvedMethodsExceptions : Exception
    {
        private readonly MethodDescription[] _unresolvedMethods;

        public UnresolvedMethodsExceptions(IEnumerable<MethodDescription> unresolvedMethods) : base("Unable to resolve one or more methods")
        {
            _unresolvedMethods = unresolvedMethods.ToArray();
        }



        public override string ToString()
        {
            StringBuilder text = new();
            text.AppendLine(Message);

            for (int i = 0; i < _unresolvedMethods.Length; i++)
            {
                text.Append("--> ");
                text.Append(_unresolvedMethods[i].ToString());
                text.AppendLine();
            }

            return text.ToString();
        }
    }
}
