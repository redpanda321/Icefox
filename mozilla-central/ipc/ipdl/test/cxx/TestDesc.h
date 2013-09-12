#ifndef mozilla_ipdltest_TestDesc_h
#define mozilla_ipdltest_TestDesc_h

#include "mozilla/_ipdltest/IPDLUnitTests.h"

#include "mozilla/_ipdltest/PTestDescParent.h"
#include "mozilla/_ipdltest/PTestDescChild.h"

#include "mozilla/_ipdltest/PTestDescSubParent.h"
#include "mozilla/_ipdltest/PTestDescSubChild.h"

#include "mozilla/_ipdltest/PTestDescSubsubParent.h"
#include "mozilla/_ipdltest/PTestDescSubsubChild.h"

namespace mozilla {
namespace _ipdltest {

//-----------------------------------------------------------------------------
// Top-level
//
class TestDescParent :
    public PTestDescParent
{
public:
    TestDescParent() { }
    virtual ~TestDescParent() { }

    static bool RunTestInProcesses() { return true; }
    static bool RunTestInThreads() { return true; }

    void Main();

    virtual bool RecvOk(PTestDescSubsubParent* a) MOZ_OVERRIDE;

protected:
    virtual PTestDescSubParent* AllocPTestDescSub(PTestDescSubsubParent*) MOZ_OVERRIDE;
    virtual bool DeallocPTestDescSub(PTestDescSubParent* actor) MOZ_OVERRIDE;

    virtual void ActorDestroy(ActorDestroyReason why) MOZ_OVERRIDE
    {
        if (NormalShutdown != why)
            fail("unexpected destruction!");  
        passed("ok");
        QuitParent();
    }
};


class TestDescChild :
    public PTestDescChild
{
public:
    TestDescChild() { }
    virtual ~TestDescChild() { }

protected:
    virtual PTestDescSubChild* AllocPTestDescSub(PTestDescSubsubChild*) MOZ_OVERRIDE;

    virtual bool DeallocPTestDescSub(PTestDescSubChild* actor) MOZ_OVERRIDE;

    virtual bool RecvTest(PTestDescSubsubChild* a) MOZ_OVERRIDE;

    virtual void ActorDestroy(ActorDestroyReason why) MOZ_OVERRIDE
    {
        if (NormalShutdown != why)
            fail("unexpected destruction!");
        QuitChild();
    }
};


//-----------------------------------------------------------------------------
// First descendent
//
class TestDescSubParent :
    public PTestDescSubParent
{
public:
    TestDescSubParent() { }
    virtual ~TestDescSubParent() { }

protected:
    virtual PTestDescSubsubParent* AllocPTestDescSubsub() MOZ_OVERRIDE;

    virtual bool DeallocPTestDescSubsub(PTestDescSubsubParent* actor) MOZ_OVERRIDE;
};


class TestDescSubChild :
    public PTestDescSubChild
{
public:
    TestDescSubChild() { }
    virtual ~TestDescSubChild() { }

protected:
    virtual PTestDescSubsubChild* AllocPTestDescSubsub() MOZ_OVERRIDE;
    virtual bool DeallocPTestDescSubsub(PTestDescSubsubChild* actor) MOZ_OVERRIDE;
};


//-----------------------------------------------------------------------------
// Grand-descendent
//
class TestDescSubsubParent :
    public PTestDescSubsubParent
{
public:
    TestDescSubsubParent() { }
    virtual ~TestDescSubsubParent() { }
};

class TestDescSubsubChild :
    public PTestDescSubsubChild
{
public:
    TestDescSubsubChild() { }
    virtual ~TestDescSubsubChild() { }
};


}
}

#endif // ifndef mozilla_ipdltest_TestDesc_h
