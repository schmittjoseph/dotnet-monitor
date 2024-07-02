// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using Microsoft.Diagnostics.Monitoring.Options;
using System.ComponentModel.DataAnnotations;
using System.Diagnostics.CodeAnalysis;
using System.Text.Json.Serialization;

namespace Microsoft.Diagnostics.Monitoring.WebApi
{
    public class StorageOptions
    {
        [Display(
            ResourceType = typeof(OptionsDisplayStrings),
            Description = nameof(OptionsDisplayStrings.DisplayAttributeDescription_StorageOptions_DefaultSharedPath))]
        public string? DefaultSharedPath { get; set; }

        [Display(
            ResourceType = typeof(OptionsDisplayStrings),
            Description = nameof(OptionsDisplayStrings.DisplayAttributeDescription_StorageOptions_DumpTempFolder))]
        public string? DumpTempFolder { get; set; }

        [Options.Experimental]
        [Display(
            ResourceType = typeof(OptionsDisplayStrings),
            Description = nameof(OptionsDisplayStrings.DisplayAttributeDescription_StorageOptions_SharedLibraryPath))]
        public string? SharedLibraryPath { get; set; }

        [JsonIgnore]
        internal bool Validated { get; set; }

        [MemberNotNullWhen(true, nameof(DefaultSharedPath), nameof(DumpTempFolder), nameof(SharedLibraryPath))]
        public bool IsValidated()
        {
            return Validated;
        }
    }
}
