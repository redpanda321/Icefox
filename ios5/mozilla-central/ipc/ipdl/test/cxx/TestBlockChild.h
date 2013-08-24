#ifndef mozilla__ipdltest_TestBlockChild_h
#define mozilla__ipdltest_TestBlockChild_h 1

#include "mozilla/_ipdltest/IPDLUnitTests.h"

#include "mozilla/_ipdltest/PTestBlockChildParent.h"
#include "mozilla/_ipdltest/PTestBlockChildChild.h"

namespace mozilla {
namespace _ipdltest {


class TestBlockChildParent :
    public PTestBlockChildParent
{
public:
    TestBlockChildParent() : mChildBlocked(false),
                             mGotP1(false),
                             mGotP2(false)
    { }
    virtual ~TestBlockChildParent() { }

    static bool RunTestInProcesses() { return true; }
    static bool RunTestInThreads() { return true; }

    void Main();

protected:
    NS_OVERRIDE
    virtual bool AnswerStackFrame();

    NS_OVERRIDE
    virtual bool RecvP1();

    NS_OVERRIDE
    virtual bool RecvP2();

    NS_OVERRIDE
    virtual bool RecvDone();

    NS_OVERRIDE
    virtual void ActorDestroy(ActorDestroyReason why)
    {
        if (NormalShutdown != why)
            fail("unexpected destruction!");  
        passed("ok");
        QuitParent();
    }

private:
    void BlockChildInLowerFrame();

    bool mChildBlocked;
    bool mGotP1;
    bool mGotP2;
    bool mGotP3;
};


class TestBlockChildChild :
    public PTestBlockChildChild
{
public:
    TestBlockChildChild() { }
    virtual ~TestBlockChildChild() { }

protected:
    NS_OVERRIDE
    virtual bool RecvPoke1();

    NS_OVERRIDE
    virtual bool AnswerStackFrame();

    NS_OVERRIDE
    virtual bool RecvPoke2();

    NS_OVERRIDE
    virtual bool RecvLastPoke();

    NS_OVERRIDE
    virtual void ActorDestroy(ActorDestroyReason why)
    {
        if (NormalShutdown != why)
            fail("unexpected destruction!");
        QuitChild();
    }

private:
    void OnPoke1();
    void OnPoke2();
    void OnLastPoke();
};


} // namespace _ipdltest
} // namespace mozilla


#endif // ifndef mozilla__ipdltest_TestBlockChild_h
