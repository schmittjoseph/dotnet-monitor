// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

#include "corhlpr.h"
#include "Snapshot.h"
#include <functional>
#include <memory>
#include "InsertProbes.h"
#include "../Utilities/NameCache.h"
#include "../Utilities/TypeNameUtilities.h"

using namespace std;

#define IfFailLogRet(EXPR) IfFailLogRet_(m_pLogger, EXPR)

#define ENUM_BUFFER_SIZE 10

Snapshot::Snapshot(const shared_ptr<ILogger>& logger, ICorProfilerInfo12* profilerInfo)
{
    m_pLogger = logger;
    m_pCorProfilerInfo = profilerInfo;
}

#ifdef NAME_RESOLVER
HRESULT SplitFqName(tstring name, tstring &split, size_t startIndex = 0) {
    size_t seperatorIndex = startIndex;
    size_t length = name.length();
    while (seperatorIndex < length)
    {
        if (name[seperatorIndex] == _T('!'))
        {
            break;
        }
        seperatorIndex++;
    }

    split = tstring(name, startIndex, seperatorIndex - startIndex);

    return S_OK;
}


HRESULT Snapshot::GetFunctionIDFromName(tstring name, FunctionID* pFuncId)
{
    HRESULT hr = S_OK;

    // Alternative: Have the startup hook call into us with the method ids.
    // Need to validate.

    TypeNameUtilities typeNameUtilities(m_pCorProfilerInfo);
    NameCache cache;

    tstring desiredModuleName;
    IfFailLogRet(SplitFqName(name, desiredModuleName));

    tstring desiredClassName;
    IfFailLogRet(SplitFqName(name, desiredClassName, desiredModuleName.length() + 1));

    tstring desiredMethodName;
    IfFailLogRet(SplitFqName(name, desiredMethodName, desiredModuleName.length() + desiredClassName.length() + 2));

    // Step 1: ModuleID
    // ModuleID moduleId;

    ComPtr<ICorProfilerModuleEnum> pEnum = NULL;
    ModuleID curModule;
    mdMethodDef tkMatchedMethodDef = mdTokenNil;
    mdTypeDef tkMatchedClass = mdTokenNil;

    IfFailLogRet(m_pCorProfilerInfo->EnumModules(&pEnum));
    while (pEnum->Next(1, &curModule, NULL) == S_OK) {
        tstring modName;
        IfFailLogRet(typeNameUtilities.GetModuleName(curModule, modName));

        if (modName != desiredModuleName) {
            continue;
        }

        m_pLogger->Log(LogLevel::Warning, _LS("Found module!"));
        ComPtr<IMetaDataImport> pMetadataImport;
        IfFailLogRet(m_pCorProfilerInfo->GetModuleMetaData(curModule, ofRead, IID_IMetaDataImport, reinterpret_cast<IUnknown **>(&pMetadataImport)));

        // Search for the class.
        mdTypeDef tkClassDef = mdTokenNil;
        IfFailLogRet(pMetadataImport->FindTypeDefByName(desiredClassName.c_str(), mdTokenNil, &tkClassDef));
        if (tkClassDef == mdTokenNil) {
            continue;
        }
        m_pLogger->Log(LogLevel::Warning, _LS("Found class!"));

        // JSFIX: Supported nested classes (+)

        // Search for the function
        // EnumMethodsWithName https://learn.microsoft.com/en-us/dotnet/framework/unmanaged-api/metadata/imetadataimport-enummethodswithname-method
        // metadataImport->EnumMethodsWithName()
        HCORENUM hEnum = 0;
        mdMethodDef md[ENUM_BUFFER_SIZE];
        ULONG count = 0;

        // GetFunctionFromTokenAndTypeArgs
        while ((hr = pMetadataImport->EnumMethodsWithName(&hEnum, tkClassDef, desiredMethodName.c_str(), md, ENUM_BUFFER_SIZE, &count)) == S_OK)
        {
            for (ULONG i = 0; i < count; i++) {

                // JSFIX: Check signature / types
                DWORD dwAttr;
                PCCOR_SIGNATURE sigParam;
                ULONG cbSigParam;
                DWORD dwImplFlags;
                mdTypeDef classToken = tkClassDef;
                hr = pMetadataImport->GetMethodProps(
                    md[i],
                    &classToken,
                    NULL, // method name
                    0, // size of method name
                    NULL, // actual size
                    &dwAttr,
                    &sigParam,
                    &cbSigParam,
                    NULL, // rva
                    &dwImplFlags);
                
                if (hr != S_OK) {
                    pMetadataImport->CloseEnum(hEnum);
                    return hr;
                }
                
                // Verify the signature matches
                tkMatchedMethodDef = md[i];
                tkMatchedClass = classToken;
                break;
                // 1. return value.

            }

            if (tkMatchedMethodDef != mdTokenNil) {
                break;
            }
        }
        
        if (hEnum) {
            pMetadataImport->CloseEnum(hEnum);
        }

        if (tkMatchedMethodDef == mdTokenNil) {
            return E_FAIL;
        }

        break;
    }

    FunctionID funcId = 0;
    // Step 2: FunctionID
    IfFailLogRet(m_pCorProfilerInfo->GetFunctionFromTokenAndTypeArgs(curModule, tkMatchedMethodDef, tkMatchedClass, 0, NULL, &funcId));


    *pFuncId = funcId;
    return S_OK;
}
#endif

HRESULT Snapshot::RegisterFunctionProbes(FunctionID enterProbeID, FunctionID leaveProbeID)
{
    m_enterHookId = enterProbeID;
    m_leaveHookId = leaveProbeID;

    m_pLogger->Log(LogLevel::Information, _LS("Probes received"));

    return S_OK;
}

HRESULT Snapshot::ResolveAllProbes()
{
    return S_OK;
}

HRESULT Snapshot::Enable(tstring name)
{
    HRESULT hr = S_OK;

    IfFailLogRet(ResolveAllProbes());

    m_pLogger->Log(LogLevel::Warning, _LS("Enabling snapshotter"));

/*
    tstring expected = _T("Mvc.dll!Benchmarks.Controllers.JsonController!JsonNk");

    FunctionID funcId;
    IfFailLogRet(GetFunctionIDFromName(expected, &funcId));
*/
    // JSFIX: Using leave hook as the test func.
    FunctionID funcId = m_leaveHookId;
    // JSFIX: Ensure we jit the function first before attempting a rejit.

    const long numberOfFunctions = 1;

    ModuleID moduleId;
    mdToken methodDef;

    IfFailLogRet(m_pCorProfilerInfo->GetFunctionInfo2(funcId,
                                            NULL,
                                            NULL,
                                            &moduleId,
                                            &methodDef,
                                            0,
                                            NULL,
                                            NULL));

    m_EnabledModuleIds.push_back(moduleId);
    m_EnabledMethodDefs.push_back(methodDef);

    IfFailLogRet(m_pCorProfilerInfo->RequestReJITWithInliners(
        COR_PRF_REJIT_BLOCK_INLINING | COR_PRF_REJIT_INLINING_CALLBACKS,
        (ULONG)m_EnabledModuleIds.size(),
        m_EnabledModuleIds.data(),
        m_EnabledMethodDefs.data()));

    // Store the module ids & method defs so we know what needs to be backed out.

    return S_OK;
}


HRESULT Snapshot::Disable()
{
    HRESULT hr = S_OK;

    m_pLogger->Log(LogLevel::Warning, _LS("Disabling snapshotter"));

    IfFailLogRet(m_pCorProfilerInfo->RequestRevert(
        (ULONG)m_EnabledModuleIds.size(),
        m_EnabledModuleIds.data(),
        m_EnabledMethodDefs.data(),
        nullptr));

    m_EnabledModuleIds.clear();
    m_EnabledMethodDefs.clear();

    return S_OK;
}

BOOL Snapshot::IsEnabled() {
    return m_EnabledModuleIds.size() != 0;
}

void Snapshot::AddProfilerEventMask(DWORD& eventsLow)
{
    m_pLogger->Log(LogLevel::Debug, _LS("Configuring snapshotter."));
    eventsLow |= COR_PRF_MONITOR::COR_PRF_ENABLE_REJIT | COR_PRF_MONITOR_JIT_COMPILATION;
}

mdMethodDef Snapshot::GetMethodDefForFunction(FunctionID functionId)
{
    mdToken token = mdTokenNil;

    HRESULT hr = S_OK;
    // JSFIX: Check ret
    hr = m_pCorProfilerInfo->GetFunctionInfo2(functionId,
                                            NULL,
                                            NULL,
                                            NULL,
                                            &token,
                                            0,
                                            NULL,
                                            NULL);

    return token;
}

HRESULT STDMETHODCALLTYPE Snapshot::ReJITHandler(ModuleID moduleId, mdMethodDef methodId, ICorProfilerFunctionControl* pFunctionControl)
{
    HRESULT hr = S_OK;

#pragma region resolve_method_defs

    FunctionID functionId;
    IfFailLogRet(m_pCorProfilerInfo->GetFunctionFromToken(moduleId,
                                                methodId,
                                                &functionId));

    ComPtr<IMetaDataImport> metadataImport;
    IfFailLogRet(m_pCorProfilerInfo->GetModuleMetaData(moduleId, ofRead | ofWrite, IID_IMetaDataImport, reinterpret_cast<IUnknown **>(&metadataImport)));

    PCCOR_SIGNATURE sigParam;
    ULONG cbSigParam;
    IfFailLogRet(metadataImport->GetMethodProps(methodId, NULL, NULL, 0, NULL, NULL, &sigParam, &cbSigParam, NULL, NULL));

    ComPtr<IMetaDataEmit> metadataEmit;
    IfFailLogRet(metadataImport->QueryInterface(IID_IMetaDataEmit, reinterpret_cast<void **>(&metadataEmit)));



    mdMethodDef enterDef = GetMethodDefForFunction(m_enterHookId);
    mdMethodDef leaveDef = GetMethodDefForFunction(m_leaveHookId);
#pragma endregion

    struct CorLibTypeTokens tokens;
    IfFailRet(EmitNecessaryCorLibTypeTokens(metadataImport, metadataEmit, &tokens));
    m_pLogger->Log(LogLevel::Debug, _LS("Emitted necessary assembly and type refs and into target assembly"));  

    // Todo: Pass all primitive tokens.
    IfFailLogRet(InsertProbes(
        m_pCorProfilerInfo,
        pFunctionControl,
        moduleId,
        methodId,
        functionId,
        enterDef,
        leaveDef,
        sigParam,
        cbSigParam,
        &tokens));

    return S_OK;
}

HRESULT Snapshot::ResolveCorLib(ModuleID *pCorLibModuleId)
{
    if (m_resolvedCorLibId != 0) {
        *pCorLibModuleId = m_resolvedCorLibId;
        return S_OK;
    }

    HRESULT hr = S_OK;
    *pCorLibModuleId = 0;
    ModuleID candidateModuleId = 0;
    ComPtr<ICorProfilerModuleEnum> pEnum = NULL;
    ModuleID curModule;
    mdTypeDef sysObjectTypeDef = mdTypeDefNil;

    IfFailLogRet(m_pCorProfilerInfo->EnumModules(&pEnum));
    while (pEnum->Next(1, &curModule, NULL) == S_OK) {
        //
        // In the CoreCLR with reference assemblies and redirection it is difficult to determine if
        // a particular Assembly is the System assembly.
        //
        // In the CoreCLR runtimes, the System assembly can be System.Private.CoreLib.dll, System.Runtime.dll
        // or netstandard.dll and in the future a different Assembly name could be used.
        //
        // Determine the identity of the System assembly by querying if the Assembly defines the
        // well known type System.Object as that type must be defined by the System assembly
        //
        mdTypeDef tkObjectTypeDef = mdTypeDefNil;

        ComPtr<IMetaDataImport> curMetadataImporter;
        IfFailLogRet(m_pCorProfilerInfo->GetModuleMetaData(curModule, ofRead | ofWrite, IID_IMetaDataImport, reinterpret_cast<IUnknown **>(&curMetadataImporter)));

        if (curMetadataImporter->FindTypeDefByName(_T("System.Object"), mdTokenNil, &tkObjectTypeDef) != S_OK) {
            continue;
        }

        DWORD dwClassAttrs = 0;
        mdToken tkExtends = mdTokenNil;
        if (curMetadataImporter->GetTypeDefProps(tkObjectTypeDef, NULL, NULL, 0, &dwClassAttrs, &tkExtends) != S_OK) {
            continue;
        }

        bool bExtends = curMetadataImporter->IsValidToken(tkExtends);
        bool isClass = ((dwClassAttrs & tdClassSemanticsMask) == tdClass);

        // We also check the type properties to make sure that we have a class and not a Value type definition
        // and that this type definition isn't extending another type.
        if (isClass & !bExtends)
        {
            candidateModuleId = curModule;
            sysObjectTypeDef = tkObjectTypeDef;
            break;
        }
    }

    if (sysObjectTypeDef == mdTypeDefNil) {
        return E_FAIL;
    }

    *pCorLibModuleId = m_resolvedCorLibId = candidateModuleId;
    return S_OK;
}


HRESULT Snapshot::GetTokenForExistingCorLibAssemblyRef(ComPtr<IMetaDataImport> pMetadataImport, ComPtr<IMetaDataEmit> pMetadataEmit, mdAssemblyRef* pTkMscorlibAssemblyRef)
{
    #define NETCORECORLIB _T("System.Private.CoreLib") // JSFIX: Not true,  need to calculate this from ResolveCorLib
    #define NETCORECORLIB_NAME_LENGTH (sizeof(NETCORECORLIB)/sizeof(TCHAR))

    HRESULT hr = S_OK;
    *pTkMscorlibAssemblyRef = mdAssemblyRefNil;

    ModuleID corlibID;
    IfFailLogRet(ResolveCorLib(&corlibID));

    ComPtr<IMetaDataAssemblyImport> pMetadataAssemblyImport;
    IfFailLogRet(pMetadataImport->QueryInterface(IID_IMetaDataAssemblyImport, reinterpret_cast<void **>(&pMetadataAssemblyImport)));

    HCORENUM hEnum = 0;
    mdAssemblyRef mdRefs[ENUM_BUFFER_SIZE];
    ULONG count = 0;

    const tstring expectedName = tstring(NETCORECORLIB);

    // We only need a buffer of equal size to NETCORECORLIB.
    const ULONG expectedLength = NETCORECORLIB_NAME_LENGTH;
    TCHAR assemblyName[expectedLength]; 

    while ((hr = pMetadataAssemblyImport->EnumAssemblyRefs(&hEnum, mdRefs, ENUM_BUFFER_SIZE, &count)) == S_OK)
    {
        for (ULONG i = 0; i < count; i++)
        {
            mdAssemblyRef curRef = mdRefs[i];

            // Get the name.
            ULONG cchName = 0;
            hr = pMetadataAssemblyImport->GetAssemblyRefProps(
                curRef,
                NULL, NULL, // public key or token
                assemblyName, expectedLength, &cchName, // name
                NULL, // metadata
                NULL, NULL, // hash value
                NULL); // flags

            // Current assembly's name is longer than corlib's
            if (hr == CLDB_S_TRUNCATION) {
                continue;
            } else if (hr != S_OK) {
                return hr;
            }

            if (cchName != expectedLength) {
                continue;
            }

            tstring assemblyNameStr = tstring(assemblyName);
            if (assemblyNameStr == expectedName) {
                pMetadataAssemblyImport->CloseEnum(hEnum);
                *pTkMscorlibAssemblyRef = curRef;
                return S_OK;
            }
        }
    }

    if (hEnum) {
        pMetadataAssemblyImport->CloseEnum(hEnum);
    }

    /* First emit the corlib assembly ref */
    ComPtr<IMetaDataAssemblyEmit> pMetadataAssemblyEmit;
    IfFailLogRet(pMetadataEmit->QueryInterface(IID_IMetaDataAssemblyEmit, reinterpret_cast<void **>(&pMetadataAssemblyEmit)));

    BYTE publicKeyToken[] = { 0x7c, 0xec, 0x85, 0xd7, 0xbe, 0xa7, 0x79, 0x8e };
    ASSEMBLYMETADATA corLibMetadata{};
    corLibMetadata.usMajorVersion = 4;

    /* Get and check */

    IfFailLogRet(pMetadataAssemblyEmit->DefineAssemblyRef(
        publicKeyToken,
        8,
        NETCORECORLIB,
        &corLibMetadata,
        nullptr,
        0,
        afContentType_Default,
        pTkMscorlibAssemblyRef));

    return S_OK;
}


HRESULT Snapshot::EmitNecessaryCorLibTypeTokens(
    ComPtr<IMetaDataImport> pMetadataImport,
    ComPtr<IMetaDataEmit> pMetadataEmit,
    struct CorLibTypeTokens *pCorLibTypeTokens)
{
    HRESULT hr = S_OK;

    mdAssemblyRef tkMscorlibAssemblyRef = mdAssemblyRefNil;
    IfFailLogRet(GetTokenForExistingCorLibAssemblyRef(
        pMetadataImport,
        pMetadataEmit,
        &tkMscorlibAssemblyRef));

    IfFailRet(GetTokenForType(
        pMetadataImport,
        pMetadataEmit,
        tkMscorlibAssemblyRef,
        _T("System.Boolean"),
        &pCorLibTypeTokens->tkSystemBooleanType));

    IfFailRet(GetTokenForType(
        pMetadataImport,
        pMetadataEmit,
        tkMscorlibAssemblyRef,
        _T("System.Byte"),
        &pCorLibTypeTokens->tkSystemByteType));

    IfFailRet(GetTokenForType(
        pMetadataImport,
        pMetadataEmit,
        tkMscorlibAssemblyRef,
        _T("System.Char"),
        &pCorLibTypeTokens->tkSystemCharType));

    IfFailRet(GetTokenForType(
        pMetadataImport,
        pMetadataEmit,
        tkMscorlibAssemblyRef,
        _T("System.Double"),
        &pCorLibTypeTokens->tkSystemDoubleType));

    IfFailRet(GetTokenForType(
        pMetadataImport,
        pMetadataEmit,
        tkMscorlibAssemblyRef,
        _T("System.Int16"),
        &pCorLibTypeTokens->tkSystemInt16Type));

    IfFailRet(GetTokenForType(
        pMetadataImport,
        pMetadataEmit,
        tkMscorlibAssemblyRef,
        _T("System.Int32"),
        &pCorLibTypeTokens->tkSystemInt32Type));

    IfFailRet(GetTokenForType(
        pMetadataImport,
        pMetadataEmit,
        tkMscorlibAssemblyRef,
        _T("System.Int64"),
        &pCorLibTypeTokens->tkSystemInt64Type));

    IfFailRet(GetTokenForType(
        pMetadataImport,
        pMetadataEmit,
        tkMscorlibAssemblyRef,
        _T("System.Object"),
        &pCorLibTypeTokens->tkSystemObjectType));

    IfFailRet(GetTokenForType(
        pMetadataImport,
        pMetadataEmit,
        tkMscorlibAssemblyRef,
        _T("System.SByte"),
        &pCorLibTypeTokens->tkSystemSByteType));

    IfFailRet(GetTokenForType(
        pMetadataImport,
        pMetadataEmit,
        tkMscorlibAssemblyRef,
        _T("System.Single"),
        &pCorLibTypeTokens->tkSystemSingleType));

    IfFailRet(GetTokenForType(
        pMetadataImport,
        pMetadataEmit,
        tkMscorlibAssemblyRef,
        _T("System.String"),
        &pCorLibTypeTokens->tkSystemStringType));

    IfFailRet(GetTokenForType(
        pMetadataImport,
        pMetadataEmit,
        tkMscorlibAssemblyRef,
        _T("System.UInt16"),
        &pCorLibTypeTokens->tkSystemUInt16Type));

    IfFailRet(GetTokenForType(
        pMetadataImport,
        pMetadataEmit,
        tkMscorlibAssemblyRef,
        _T("System.UInt32"),
        &pCorLibTypeTokens->tkSystemUInt32Type));

    IfFailRet(GetTokenForType(
        pMetadataImport,
        pMetadataEmit,
        tkMscorlibAssemblyRef,
        _T("System.UInt64"),
        &pCorLibTypeTokens->tkSystemUInt64Type));

    IfFailRet(GetTokenForType(
        pMetadataImport,
        pMetadataEmit,
        tkMscorlibAssemblyRef,
        _T("System.IntPtr"),
        &pCorLibTypeTokens->tkSystemIntPtrType));

    IfFailRet(GetTokenForType(
        pMetadataImport,
        pMetadataEmit,
        tkMscorlibAssemblyRef,
        _T("System.UIntPtr"),
        &pCorLibTypeTokens->tkSystemUIntPtrType));

    return S_OK;
}


HRESULT Snapshot::GetTokenForType(
    ComPtr<IMetaDataImport> pMetadataImport,
    ComPtr<IMetaDataEmit> pMetadataEmit,
    mdToken tkResolutionScope,
    tstring name,
    mdToken* ptkType)
{
    HRESULT hr = S_OK;

    *ptkType = mdTokenNil;

    mdTypeRef tkType;
    hr = pMetadataImport->FindTypeDefByName(
        name.c_str(),
        mdTokenNil,
        &tkType);

    if (FAILED(hr) || tkType == mdTokenNil)
    {
        IfFailRet(pMetadataEmit->DefineTypeRefByName(
            tkResolutionScope,
            name.c_str(),
            &tkType));
    }

    *ptkType = tkType;
    return S_OK;
}