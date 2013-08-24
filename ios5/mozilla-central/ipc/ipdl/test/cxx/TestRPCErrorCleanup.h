#ifndef mozilla__ipdltest_TestRPCErrorCleanup_h
#define mozilla__ipdltest_TestRPCErrorCleanup_h 1

#include "mozilla/_ipdltest/IPDLUnitTests.h"

#include "mozilla/_ipdltest/PTestRPCErrorCleanupParent.h"
#include "mozilla/_ipdltest/PTestRPCErrorCleanupChild.h"

namespace mozilla {
namespace _ipdltest {


class TestRPCErrorCleanupParent :
    public PTestRPCErrorCleanupParent
{
public:
    TestRPCErrorCleanupParent();
    virtual ~TestRPCErrorCleanupParent();

    static bool RunTestInProcesses() { return true; }
    // FIXME/bug 703323 Could work if modified
    static bool RunTestInThreads() { return false; }

    void Main();

protected:    
    NS_OVERRIDE
    virtual void ActorDestroy(ActorDestroyReason why)
    {
        if (AbnormalShutdown != why)
            fail("unexpected destruction!");  
    }

    NS_OVERRIDE
    virtual void ProcessingError(Result what);

    bool mGotProcessingError;
};


class TestRPCErrorCleanupChild :
    public PTestRPCErrorCleanupChild
{
public:
    TestRPCErrorCleanupChild();
    virtual ~TestRPCErrorCleanupChild();

protected:
    NS_OVERRIDE
    virtual bool AnswerError();

    NS_OVERRIDE
    virtual void ActorDestroy(ActorDestroyReason why)
    {
        fail("should have 'crashed'!");
    }
};


} // namespace _ipdltest
} // namespace mozilla


#endif // ifndef mozilla__ipdltest_TestRPCErrorCleanup_h
