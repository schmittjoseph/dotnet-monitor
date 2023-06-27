// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using Microsoft.Diagnostics.Monitoring.TestCommon;
using System;
using System.Collections.Generic;
using System.Linq;
using Xunit;

namespace Microsoft.Diagnostics.Monitoring.StartupHook.MonitorMessageDispatcher
{
    [TargetFrameworkMonikerTrait(TargetFrameworkMoniker.Current)]
    public sealed class MonitorMessageDispatcherTests
    {
        private struct SecondSamplePayload
        {
            public int SampleInt { get; set; }
        }

        private struct SamplePayload : IEquatable<SamplePayload>
        {
            public string SampleString { get; set; }
            public List<int> SampleList { get; set; }

            public override bool Equals(object? obj)
            {
                return obj is SamplePayload payload && Equals(payload);
                       
            }

            public bool Equals(SamplePayload other)
            {
                return SampleString == other.SampleString &&
                       Enumerable.SequenceEqual(SampleList, other.SampleList);
            }

            public override int GetHashCode()
            {
                return HashCode.Combine(SampleString, SampleList);
            }
        }

        [Fact]
        public void Dispatcher_DeserializesCorrectly()
        {
            // Arrange
            using MockMessageSource messageSource = new();
            using MonitorMessageDispatcher dispatcher = new(messageSource);

            SamplePayload expectedPayload = new SamplePayload
            {
                SampleString = "HelloWorld",
                SampleList = new List<int> { 1, 2 },
            };

            bool didGetCallback = false;
            dispatcher.RegisterCallback<SamplePayload>(ProfilerMessageType.Callstack, (payload) =>
            {
                didGetCallback = true;
                Assert.Equal(expectedPayload, payload);
            });

            // Act
            messageSource.RaiseMessage(ProfilerMessageType.Callstack, expectedPayload);

            // Assert
            Assert.True(didGetCallback);
        }

        [Fact]
        public void Dispatcher_RejectsMismatchingPayloadTypes()
        {
            // Arrange
            using MockMessageSource messageSource = new();
            using MonitorMessageDispatcher dispatcher = new(messageSource);

            SecondSamplePayload expectedPayload = new SecondSamplePayload
            {
                SampleInt = 10
            };

            bool didGetCallback = false;
            dispatcher.RegisterCallback<SamplePayload>(ProfilerMessageType.Callstack, (payload) =>
            {
                didGetCallback = true;
            });

            // Act
            messageSource.RaiseMessage(ProfilerMessageType.Callstack, expectedPayload);

            // Assert
            Assert.True(didGetCallback);
        }
    }
}
