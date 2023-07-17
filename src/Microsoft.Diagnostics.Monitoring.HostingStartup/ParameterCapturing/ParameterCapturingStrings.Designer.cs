﻿//------------------------------------------------------------------------------
// <auto-generated>
//     This code was generated by a tool.
//     Runtime Version:4.0.30319.42000
//
//     Changes to this file may cause incorrect behavior and will be lost if
//     the code is regenerated.
// </auto-generated>
//------------------------------------------------------------------------------

namespace Microsoft.Diagnostics.Monitoring.HostingStartup.ParameterCapturing {
    using System;
    
    
    /// <summary>
    ///   A strongly-typed resource class, for looking up localized strings, etc.
    /// </summary>
    // This class was auto-generated by the StronglyTypedResourceBuilder
    // class via a tool like ResGen or Visual Studio.
    // To add or remove a member, edit your .ResX file then rerun ResGen
    // with the /str option, or rebuild your VS project.
    [global::System.CodeDom.Compiler.GeneratedCodeAttribute("System.Resources.Tools.StronglyTypedResourceBuilder", "17.0.0.0")]
    [global::System.Diagnostics.DebuggerNonUserCodeAttribute()]
    [global::System.Runtime.CompilerServices.CompilerGeneratedAttribute()]
    internal class ParameterCapturingStrings {
        
        private static global::System.Resources.ResourceManager resourceMan;
        
        private static global::System.Globalization.CultureInfo resourceCulture;
        
        [global::System.Diagnostics.CodeAnalysis.SuppressMessageAttribute("Microsoft.Performance", "CA1811:AvoidUncalledPrivateCode")]
        internal ParameterCapturingStrings() {
        }
        
        /// <summary>
        ///   Returns the cached ResourceManager instance used by this class.
        /// </summary>
        [global::System.ComponentModel.EditorBrowsableAttribute(global::System.ComponentModel.EditorBrowsableState.Advanced)]
        internal static global::System.Resources.ResourceManager ResourceManager {
            get {
                if (object.ReferenceEquals(resourceMan, null)) {
                    global::System.Resources.ResourceManager temp = new global::System.Resources.ResourceManager("Microsoft.Diagnostics.Monitoring.HostingStartup.ParameterCapturing.ParameterCaptu" +
                            "ringStrings", typeof(ParameterCapturingStrings).Assembly);
                    resourceMan = temp;
                }
                return resourceMan;
            }
        }
        
        /// <summary>
        ///   Overrides the current thread's CurrentUICulture property for all
        ///   resource lookups using this strongly typed resource class.
        /// </summary>
        [global::System.ComponentModel.EditorBrowsableAttribute(global::System.ComponentModel.EditorBrowsableState.Advanced)]
        internal static global::System.Globalization.CultureInfo Culture {
            get {
                return resourceCulture;
            }
            set {
                resourceCulture = value;
            }
        }
        
        /// <summary>
        ///   Looks up a localized string similar to null.
        /// </summary>
        internal static string NullArgumentValue {
            get {
                return ResourceManager.GetString("NullArgumentValue", resourceCulture);
            }
        }
        
        /// <summary>
        ///   Looks up a localized string similar to in.
        /// </summary>
        internal static string ParameterModifier_In {
            get {
                return ResourceManager.GetString("ParameterModifier_In", resourceCulture);
            }
        }
        
        /// <summary>
        ///   Looks up a localized string similar to out.
        /// </summary>
        internal static string ParameterModifier_Out {
            get {
                return ResourceManager.GetString("ParameterModifier_Out", resourceCulture);
            }
        }
        
        /// <summary>
        ///   Looks up a localized string similar to ref.
        /// </summary>
        internal static string ParameterModifier_RefOrRefLike {
            get {
                return ResourceManager.GetString("ParameterModifier_RefOrRefLike", resourceCulture);
            }
        }
        
        /// <summary>
        ///   Looks up a localized string similar to Started Parameter Capturing for {duration} on {numberOfMethods} method(s)..
        /// </summary>
        internal static string StartParameterCapturingFormatString {
            get {
                return ResourceManager.GetString("StartParameterCapturingFormatString", resourceCulture);
            }
        }
        
        /// <summary>
        ///   Looks up a localized string similar to Stopped Parameter Capturing..
        /// </summary>
        internal static string StopParameterCapturing {
            get {
                return ResourceManager.GetString("StopParameterCapturing", resourceCulture);
            }
        }
        
        /// <summary>
        ///   Looks up a localized string similar to this.
        /// </summary>
        internal static string ThisParameterName {
            get {
                return ResourceManager.GetString("ThisParameterName", resourceCulture);
            }
        }
        
        /// <summary>
        ///   Looks up a localized string similar to Could not resolve method {methodName}.
        /// </summary>
        internal static string UnableToResolveMethod {
            get {
                return ResourceManager.GetString("UnableToResolveMethod", resourceCulture);
            }
        }
        
        /// <summary>
        ///   Looks up a localized string similar to unknown.
        /// </summary>
        internal static string UnknownArgumentValue {
            get {
                return ResourceManager.GetString("UnknownArgumentValue", resourceCulture);
            }
        }
        
        /// <summary>
        ///   Looks up a localized string similar to &lt;unknown_name_at_position_{0}&gt;.
        /// </summary>
        internal static string UnknownParameterNameFormatString {
            get {
                return ResourceManager.GetString("UnknownParameterNameFormatString", resourceCulture);
            }
        }
        
        /// <summary>
        ///   Looks up a localized string similar to Unable to resolve one or more methods {0}.
        /// </summary>
        internal static string UnresolvedMethodsFormatString {
            get {
                return ResourceManager.GetString("UnresolvedMethodsFormatString", resourceCulture);
            }
        }
        
        /// <summary>
        ///   Looks up a localized string similar to unsupported.
        /// </summary>
        internal static string UnsupportedParameter {
            get {
                return ResourceManager.GetString("UnsupportedParameter", resourceCulture);
            }
        }
    }
}
