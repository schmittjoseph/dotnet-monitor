// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

#pragma once

#include "../ProfilerBase.h"
#include "../Environment/Environment.h"
#include "../Environment/EnvironmentHelper.h"
#include "../Logging/Logger.h"
#include "../Communication/CommandServer.h"
#include "../Utilities/ThreadNameCache.h"
#include <memory>
#include "../ProbeInstrumentation/ProbeInstrumentation.h"

#ifdef DOTNETMONITOR_FEATURE_EXCEPTIONS
#include "ThreadDataManager.h"
#include "ExceptionTracker.h"
#endif // DOTNETMONITOR_FEATURE_EXCEPTIONS

class MainProfiler final :
    public ProfilerBase
{
public:
    static std::shared_ptr<MainProfiler> s_profiler;

private:
    std::shared_ptr<IEnvironment> m_pEnvironment;
    std::shared_ptr<EnvironmentHelper> _environmentHelper;
    std::shared_ptr<ILogger> m_pLogger;
    std::shared_ptr<ThreadNameCache> _threadNameCache;

#ifdef DOTNETMONITOR_FEATURE_EXCEPTIONS
    std::shared_ptr<ThreadDataManager> _threadDataManager;
    std::unique_ptr<ExceptionTracker> _exceptionTracker;
#endif // DOTNETMONITOR_FEATURE_EXCEPTIONS
    std::unique_ptr<ProbeInstrumentation> m_pProbeInstrumentation;

    BOOL m_isMainProfiler;


public:
    static GUID GetClsid();

    STDMETHOD(Initialize)(IUnknown* pICorProfilerInfoUnk) override;
    STDMETHOD(Shutdown)() override;
    STDMETHOD(ThreadCreated)(ThreadID threadId) override;
    STDMETHOD(ThreadDestroyed)(ThreadID threadId) override;
    STDMETHOD(ThreadNameChanged)(ThreadID threadId, ULONG cchName, WCHAR name[]) override;
    STDMETHOD(ExceptionThrown)(ObjectID thrownObjectId) override;
    STDMETHOD(ExceptionSearchCatcherFound)(FunctionID functionId) override;
    STDMETHOD(ExceptionUnwindFunctionEnter)(FunctionID functionId) override;
    STDMETHOD(InitializeForAttach)(IUnknown* pCorProfilerInfoUnk, void* pvClientData, UINT cbClientData) override;
    STDMETHOD(LoadAsNotficationOnly)(BOOL *pbNotificationOnly) override;

    STDMETHOD(GetReJITParameters)(ModuleID moduleId, mdMethodDef methodId, ICorProfilerFunctionControl* pFunctionControl) override;
    STDMETHOD(ReJITError)(ModuleID moduleId, mdMethodDef methodId, FunctionID functionId, HRESULT hrStatus) override;
    STDMETHOD(ReJITCompilationFinished)(FunctionID functionId, ReJITID rejitId, HRESULT hrStatus, BOOL fIsSafeToBlock) override;

public:
    STDMETHOD(RegisterFunctionProbe)(FunctionID enterProbeId);
    STDMETHOD(RequestFunctionProbeInstallation)(UINT64 functionIds[], ULONG count, UINT32 boxingTokens[], ULONG boxingTokenCounts[]);
    STDMETHOD(RequestFunctionProbeShutdown)();

private:
    HRESULT InitializeCommon();
    HRESULT InitializeEnvironment();
    HRESULT InitializeEnvironmentHelper();
    HRESULT InitializeLogging();
    HRESULT InitializeCommandServer();
    HRESULT MessageCallback(const IpcMessage& message);
    HRESULT ProcessCallstackMessage();
private:
    std::unique_ptr<CommandServer> _commandServer;
};

