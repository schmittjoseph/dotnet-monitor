﻿// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using System.Collections.Generic;
using System.ComponentModel.DataAnnotations;
using System.Diagnostics.CodeAnalysis;

namespace Microsoft.Diagnostics.Tools.Monitor.CollectionRules.Actions
{
    /// <summary>
    /// Provides operations over collection rule actions.
    /// </summary>
    internal interface ICollectionRuleActionOperations
    {
        /// <summary>
        /// Attempts to create a proxy for the action instance
        /// associated with the registered action name.
        /// </summary>
        bool TryCreateFactory(
            string actionName,
            [NotNullWhen(true)] out ICollectionRuleActionFactoryProxy? action);

        /// <summary>
        /// Attempts to create an options instance of the options type
        /// associated with the registered action name.
        /// </summary>
        bool TryCreateOptions(
            string actionName,
            [NotNullWhen(true)] out object? options);

        /// <summary>
        /// Attempts to validate an options instance of the options type
        /// associated with the registered action name.
        /// </summary>
        bool TryValidateOptions(
            string actionName,
            object options,
            ValidationContext validationContext,
            ICollection<ValidationResult> results);
    }
}
