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
#include "../Snapshot/Snapshot.h"

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
    std::shared_ptr<EnvironmentHelper> m_pEnvironmentHelper;
    std::shared_ptr<ILogger> m_pLogger;
    std::shared_ptr<ThreadNameCache> _threadNameCache;

    std::unordered_map<tstring, FunctionID> _functionNames;
    std::mutex _mutex;

#ifdef DOTNETMONITOR_FEATURE_EXCEPTIONS
    std::shared_ptr<ThreadDataManager> _threadDataManager;
    std::unique_ptr<ExceptionTracker> _exceptionTracker;
#endif // DOTNETMONITOR_FEATURE_EXCEPTIONS
    std::unique_ptr<Snapshot> m_pSnapshotter;

    BOOL m_isMainProfiler;
    INT32 m_Level;


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
    STDMETHOD(JITCompilationStarted)(FunctionID functionId, BOOL fIsSafeToBlock) override;
    STDMETHOD(GetReJITParameters)(ModuleID moduleId, mdMethodDef methodId, ICorProfilerFunctionControl* pFunctionControl) override;

    STDMETHOD_(BSTR, GetLogMessage)(PINT32 level);

private:
    HRESULT InitializeCommon();
    HRESULT InitializeEnvironment();
    HRESULT InitializeEnvironmentHelper();
    HRESULT InitializeLogging();
    HRESULT InitializeCommandServer();
    HRESULT MessageCallback(const IpcMessage& message);
    HRESULT ProcessCallstackMessage();
    tstring MainProfiler::GetFunctionIDName(FunctionID funcId);
    tstring MainProfiler::GetClassIDName(ClassID classId);

private:
    std::unique_ptr<CommandServer> _commandServer;
};

