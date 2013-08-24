#ifndef mozilla__ipdltest_TestHangs_h
#define mozilla__ipdltest_TestHangs_h 1

#include "mozilla/_ipdltest/IPDLUnitTests.h"

#include "mozilla/_ipdltest/PTestHangsParent.h"
#include "mozilla/_ipdltest/PTestHangsChild.h"

namespace mozilla {
namespace _ipdltest {


class TestHangsParent :
    public PTestHangsParent
{
public:
    TestHangsParent();
    virtual ~TestHangsParent();

    void Main();

protected:
    NS_OVERRIDE
    virtual bool ShouldContinueFromReplyTimeout();

    NS_OVERRIDE
    virtual bool RecvNonce() {
        return true;
    }

    NS_OVERRIDE
    virtual bool AnswerStackFrame();

    NS_OVERRIDE
    virtual void ActorDestroy(ActorDestroyReason why)
    {
        if (AbnormalShutdown != why)
            fail("unexpected destruction!");  
        passed("ok");
        QuitParent();
    }

    void CleanUp();

    bool mDetectedHang;
};


class TestHangsChild :
    public PTestHangsChild
{
public:
    TestHangsChild();
    virtual ~TestHangsChild();

protected:
    NS_OVERRIDE
    virtual bool RecvStart() {
        if (!SendNonce())
            fail("sending Nonce");
        return true;
    }

    NS_OVERRIDE
    virtual bool AnswerStackFrame()
    {
        if (CallStackFrame())
            fail("should have failed");
        return true;
    }

    NS_OVERRIDE
    virtual bool AnswerHang();

    NS_OVERRIDE
    virtual void ActorDestroy(ActorDestroyReason why)
    {
        if (AbnormalShutdown != why)
            fail("unexpected destruction!");
        QuitChild();
    }
};


} // namespace _ipdltest
} // namespace mozilla


#endif // ifndef mozilla__ipdltest_TestHangs_h
