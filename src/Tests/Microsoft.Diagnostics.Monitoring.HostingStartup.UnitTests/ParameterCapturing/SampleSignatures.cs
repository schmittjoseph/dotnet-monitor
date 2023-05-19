// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using System;
using System.Collections.Generic;

namespace SampleSignatures
{
    internal struct MyTestStruct { }
    internal ref struct MyRefStruct { }
    internal enum MyEnum { }

#pragma warning disable CA1822 // Mark members as static
    internal sealed class TestMethodSignatures
    {
        public void ImplicitThis() { }
    }


#pragma warning restore CA1822 // Mark members as static

    internal static class StaticTestMethodSignatures
    {
        public static void BasicTypes(string s, int[] intArray, bool[,] multidimensionalArray, uint uInt) { }

        public static void NoArgs() { }

        public static void ExplicitThis(this object thisObj) { }

        public static void RefStruct(ref MyRefStruct myRefStruct) { }

        public static unsafe void Pointer(byte* test) { }

        public static void RefParam(ref int i) { }

        public static void OutParam(out int i)
        {
            i = 0;
        }

        // JSFIX: Restrict this until value-type support is fully done.
        public static void GenericParameters<T, K>(T t, K k) { }


        public static void TypeRef(Uri uri) { }

        public static void TypeDef(TestMethodSignatures t) { }

        public static void TypeSpec(IList<IEnumerable<bool>> list) { }


        public static void ValueTypeDef(MyEnum myEnum) { }

        public static void ValueTypeRef(TypeCode typeCode) { }

        public static void ValueTypeSpec(bool? b) { }

        public static void Unicode_ΦΨ(bool δ) { }

    }
}
