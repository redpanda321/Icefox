/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=4 sw=4 et tw=78:
 *
 * ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is Mozilla Communicator client code, released
 * March 31, 1998.
 *
 * The Initial Developer of the Original Code is
 * Netscape Communications Corporation.
 * Portions created by the Initial Developer are Copyright (C) 1998
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either of the GNU General Public License Version 2 or later (the "GPL"),
 * or the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

#ifndef jsinterp_h___
#define jsinterp_h___
/*
 * JS interpreter interface.
 */
#include "jsprvtd.h"
#include "jspubtd.h"
#include "jsfun.h"
#include "jsopcode.h"
#include "jsscript.h"
#include "jsvalue.h"

typedef struct JSFrameRegs {
    JSStackFrame    *fp;            /* active frame */
    jsbytecode      *pc;            /* program counter */
    js::Value       *sp;            /* stack pointer */
} JSFrameRegs;

/* JS stack frame flags. */
enum JSFrameFlags {
    JSFRAME_CONSTRUCTING       =  0x01, /* frame is for a constructor invocation */
    JSFRAME_COMPUTED_THIS      =  0x02, /* frame.thisv was computed already and
                                           JSVAL_IS_OBJECT(thisv) */
    JSFRAME_ASSIGNING          =  0x04, /* a complex (not simplex JOF_ASSIGNING) op
                                           is currently assigning to a property */
    JSFRAME_DEBUGGER           =  0x08, /* frame for JS_EvaluateInStackFrame */
    JSFRAME_EVAL               =  0x10, /* frame for obj_eval */
    JSFRAME_FLOATING_GENERATOR =  0x20, /* frame copy stored in a generator obj */
    JSFRAME_YIELDING           =  0x40, /* js_Interpret dispatched JSOP_YIELD */
    JSFRAME_GENERATOR          =  0x80, /* frame belongs to generator-iterator */
    JSFRAME_OVERRIDE_ARGS      = 0x100, /* overridden arguments local variable */
    JSFRAME_DUMMY              = 0x200, /* frame is a dummy frame */
    JSFRAME_IN_IMACRO          = 0x400, /* frame has imacpc value available */

    JSFRAME_SPECIAL            = JSFRAME_DEBUGGER | JSFRAME_EVAL
};

/*
 * JS stack frame, may be allocated on the C stack by native callers.  Always
 * allocated on cx->stackPool for calls from the interpreter to an interpreted
 * function.
 *
 * NB: This struct is manually initialized in jsinterp.c and jsiter.c.  If you
 * add new members, update both files.
 */
struct JSStackFrame
{
  private:
    JSObject            *callobj;       /* lazily created Call object */
    JSObject            *argsobj;       /* lazily created arguments object */
    JSObject            *scopeChain;    /* current scope chain */
    JSObject            *blockChain;    /* current static block */
    jsbytecode          *imacpc;        /* null or interpreter macro call pc */
    void                *annotation;    /* used by Java security */
    void                *hookData;      /* debugger call hook data */
    JSVersion           callerVersion;  /* dynamic version of calling script */
    JSScript            *script;        /* script being interpreted */
    JSFunction          *fun;           /* function being called or null */
    js::Value           thisv;          /* "this" pointer if in method */
    js::Value           rval;           /* function return value */
    uintN               argc;           /* actual argument count */

  public:
    js::Value           *argv;          /* base of argument stack slots */

    /* Maintained by StackSpace operations */
    JSStackFrame        *down;          /* previous frame, part of
                                           stack layout invariant */
    jsbytecode          *savedPC;       /* only valid if cx->fp != this */
#ifdef DEBUG
    static jsbytecode *const sInvalidPC;
#endif

    uint32              flags;          /* frame flags -- see below */

    void                *padding;

    /* Get the frame's current bytecode, assuming |this| is in |cx|. */
    jsbytecode *pc(JSContext *cx) const;

    js::Value *argEnd() const {
        return (js::Value *)this;
    }

    js::Value *slots() const {
        return (js::Value *)(this + 1);
    }

    js::Value *base() const {
        return slots() + getScript()->nfixed;
    }

    /* Call object accessors */

    bool hasCallObj() const {
        return callobj != NULL;
    }

    JSObject* getCallObj() const {
        JS_ASSERT(hasCallObj());
        return callobj;
    }

    JSObject* maybeCallObj() const {
        return callobj;
    }

    void setCallObj(JSObject *obj) {
        callobj = obj;
    }

    static size_t offsetCallObj() {
        return offsetof(JSStackFrame, callobj);
    }

    /* Arguments object accessors */

    bool hasArgsObj() const {
        return argsobj != NULL;
    }

    JSObject* getArgsObj() const {
        JS_ASSERT(hasArgsObj());
        JS_ASSERT(!isEvalFrame());
        return argsobj;
    }

    JSObject* maybeArgsObj() const {
        return argsobj;
    }

    void setArgsObj(JSObject *obj) {
        argsobj = obj;
    }

    JSObject** addressArgsObj() {
        return &argsobj;
    }

    static size_t offsetArgsObj() {
        return offsetof(JSStackFrame, argsobj);
    }

    /*
     * We can't determine in advance which local variables can live on
     * the stack and be freed when their dynamic scope ends, and which
     * will be closed over and need to live in the heap.  So we place
     * variables on the stack initially, note when they are closed
     * over, and copy those that are out to the heap when we leave
     * their dynamic scope.
     *
     * The bytecode compiler produces a tree of block objects
     * accompanying each JSScript representing those lexical blocks in
     * the script that have let-bound variables associated with them.
     * These block objects are never modified, and never become part
     * of any function's scope chain.  Their parent slots point to the
     * innermost block that encloses them, or are NULL in the
     * outermost blocks within a function or in eval or global code.
     *
     * When we are in the static scope of such a block, blockChain
     * points to its compiler-allocated block object; otherwise, it is
     * NULL.
     *
     * scopeChain is the current scope chain, including 'call' and
     * 'block' objects for those function calls and lexical blocks
     * whose static scope we are currently executing in, and 'with'
     * objects for with statements; the chain is typically terminated
     * by a global object.  However, as an optimization, the young end
     * of the chain omits block objects we have not yet cloned.  To
     * create a closure, we clone the missing blocks from blockChain
     * (which is always current), place them at the head of
     * scopeChain, and use that for the closure's scope chain.  If we
     * never close over a lexical block, we never place a mutable
     * clone of it on scopeChain.
     *
     * This lazy cloning is implemented in js_GetScopeChain, which is
     * also used in some other cases --- entering 'with' blocks, for
     * example.
     */

    /* Scope chain accessors */

    bool hasScopeChain() const {
        return scopeChain != NULL;
    }

    JSObject* getScopeChain() const {
        JS_ASSERT(hasScopeChain());
        return scopeChain;
    }

    JSObject* maybeScopeChain() const {
        return scopeChain;
    }

    void setScopeChain(JSObject *obj) {
        scopeChain = obj;
    }

    JSObject** addressScopeChain() {
        return &scopeChain;
    }

    static size_t offsetScopeChain() {
        return offsetof(JSStackFrame, scopeChain);
    }

    /* Block chain accessors */

    bool hasBlockChain() const {
        return blockChain != NULL;
    }

    JSObject* getBlockChain() const {
        JS_ASSERT(hasBlockChain());
        return blockChain;
    }

    JSObject* maybeBlockChain() const {
        return blockChain;
    }

    void setBlockChain(JSObject *obj) {
        blockChain = obj;
    }

    /* IMacroPC accessors. */

    bool hasIMacroPC() const { return flags & JSFRAME_IN_IMACRO; }

    /*
     * @pre     hasIMacroPC
     * @return  The PC at which an imacro started executing (guaranteed non-null. The PC of the
     *          executing imacro must be in regs.pc, so the displaced
     *          original value is stored here.
     */
    jsbytecode *getIMacroPC() const {
        JS_ASSERT(flags & JSFRAME_IN_IMACRO);
        return imacpc;
    }

    /* @return  The imacro pc if hasIMacroPC; otherwise, NULL. */
    jsbytecode *maybeIMacroPC() const { return hasIMacroPC() ? getIMacroPC() : NULL; }

    void clearIMacroPC() { flags &= ~JSFRAME_IN_IMACRO; }

    void setIMacroPC(jsbytecode *newIMacPC) {
        JS_ASSERT(newIMacPC);
        JS_ASSERT(!(flags & JSFRAME_IN_IMACRO));
        imacpc = newIMacPC;
        flags |= JSFRAME_IN_IMACRO;
    }

    /* Annotation accessors */

    bool hasAnnotation() const {
        return annotation != NULL;
    }

    void* getAnnotation() const {
        JS_ASSERT(hasAnnotation());
        return annotation;
    }

    void* maybeAnnotation() const {
        return annotation;
    }

    void setAnnotation(void *annot) {
        annotation = annot;
    }

    /* Debugger hook data accessors */

    bool hasHookData() const {
        return hookData != NULL;
    }

    void* getHookData() const {
        JS_ASSERT(hasHookData());
        return hookData;
    }

    void* maybeHookData() const {
        return hookData;
    }

    void setHookData(void *data) {
        hookData = data;
    }

    /* Version accessors */

    JSVersion getCallerVersion() const {
        return callerVersion;
    }

    void setCallerVersion(JSVersion version) {
        callerVersion = version;
    }

    /* Script accessors */

    bool hasScript() const {
        return script != NULL;
    }

    JSScript* getScript() const {
        JS_ASSERT(hasScript());
        return script;
    }

    JSScript* maybeScript() const {
        return script;
    }

    size_t getFixedCount() const {
        return getScript()->nfixed;
    }

    size_t getSlotCount() const {
        return getScript()->nslots;
    }

    void setScript(JSScript *s) {
        script = s;
    }

    static size_t offsetScript() {
        return offsetof(JSStackFrame, script);
    }

    /* Function accessors */

    bool hasFunction() const {
        return fun != NULL;
    }

    JSFunction* getFunction() const {
        JS_ASSERT(hasFunction());
        return fun;
    }

    JSFunction* maybeFunction() const {
        return fun;
    }

    size_t numFormalArgs() const {
        JS_ASSERT(!isEvalFrame());
        return getFunction()->nargs;
    }

    void setFunction(JSFunction *f) {
        fun = f;
    }

    /* This-value accessors */

    const js::Value& getThisValue() {
        return thisv;
    }

    void setThisValue(const js::Value &v) {
        thisv = v;
    }

    /* Return-value accessors */

    const js::Value& getReturnValue() {
        return rval;
    }

    void setReturnValue(const js::Value &v) {
        rval = v;
    }

    void clearReturnValue() {
        rval.setUndefined();
    }

    js::Value* addressReturnValue() {
        return &rval;
    }

    static size_t offsetReturnValue() {
        return offsetof(JSStackFrame, rval);
    }

    /* Argument count accessors */

    size_t numActualArgs() const {
        JS_ASSERT(!isEvalFrame());
        return argc;
    }

    void setNumActualArgs(size_t n) {
        argc = n;
    }

    static size_t offsetNumActualArgs() {
        return offsetof(JSStackFrame, argc);
    }

    /* Other accessors */

    void putActivationObjects(JSContext *cx) {
        /*
         * The order of calls here is important as js_PutCallObject needs to
         * access argsobj.
         */
        if (hasCallObj()) {
            js_PutCallObject(cx, this);
            JS_ASSERT(!hasArgsObj());
        } else if (hasArgsObj()) {
            js_PutArgsObject(cx, this);
        }
    }

    const js::Value &calleeValue() {
        JS_ASSERT(argv);
        return argv[-2];
    }

    /* Infallible getter to return the callee object from this frame. */
    JSObject &calleeObject() const {
        JS_ASSERT(argv);
        return argv[-2].toObject();
    }

    /*
     * Fallible getter to compute the correct callee function object, which may
     * require deferred cloning due to JSScope::methodReadBarrier. For a frame
     * with null fun member, return true with *vp set from this->calleeValue(),
     * which may not be an object (it could be undefined).
     */
    bool getValidCalleeObject(JSContext *cx, js::Value *vp);

    void setCalleeObject(JSObject &callable) {
        JS_ASSERT(argv);
        argv[-2].setObject(callable);
    }

    JSObject *callee() {
        return argv ? &argv[-2].toObject() : NULL;
    }

    /*
     * Get the "variable object" (ES3 term) associated with the Execution
     * Context's VariableEnvironment (ES5 10.3). The given StackSegment
     * must contain this stack frame.
     */
    JSObject *varobj(js::StackSegment *seg) const;

    /* Short for: varobj(cx->activeSegment()). */
    JSObject *varobj(JSContext *cx) const;

    inline JSObject *getThisObject(JSContext *cx);

    bool isGenerator() const { return !!(flags & JSFRAME_GENERATOR); }
    bool isFloatingGenerator() const {
        JS_ASSERT_IF(flags & JSFRAME_FLOATING_GENERATOR, isGenerator());
        return !!(flags & JSFRAME_FLOATING_GENERATOR);
    }

    bool isDummyFrame() const { return !!(flags & JSFRAME_DUMMY); }
    bool isEvalFrame() const { return !!(flags & JSFRAME_EVAL); }

    /* Contains static assertions for member alignment, don't call. */
    inline void staticAsserts();
};

namespace js {

JS_STATIC_ASSERT(sizeof(JSStackFrame) % sizeof(Value) == 0);
static const size_t VALUES_PER_STACK_FRAME = sizeof(JSStackFrame) / sizeof(Value);

} /* namespace js */

inline void
JSStackFrame::staticAsserts()
{
    JS_STATIC_ASSERT(offsetof(JSStackFrame, rval) % sizeof(js::Value) == 0);
    JS_STATIC_ASSERT(offsetof(JSStackFrame, thisv) % sizeof(js::Value) == 0);
}

static JS_INLINE uintN
GlobalVarCount(JSStackFrame *fp)
{
    JS_ASSERT(!fp->hasFunction());
    return fp->getScript()->nfixed;
}

/*
 * Refresh and return fp->scopeChain.  It may be stale if block scopes are
 * active but not yet reflected by objects in the scope chain.  If a block
 * scope contains a with, eval, XML filtering predicate, or similar such
 * dynamically scoped construct, then compile-time block scope at fp->blocks
 * must reflect at runtime.
 */
extern JSObject *
js_GetScopeChain(JSContext *cx, JSStackFrame *fp);

/*
 * Given a context and a vector of [callee, this, args...] for a function that
 * was specified with a JSFUN_THISP_PRIMITIVE flag, get the primitive value of
 * |this| into *thisvp. In doing so, if |this| is an object, insist it is an
 * instance of clasp and extract its private slot value to return via *thisvp.
 *
 * NB: this function loads and uses *vp before storing *thisvp, so the two may
 * alias the same Value.
 */
extern JSBool
js_GetPrimitiveThis(JSContext *cx, js::Value *vp, js::Class *clasp,
                    const js::Value **vpp);

namespace js {

/*
 * For a call with arguments argv including argv[-1] (nominal |this|) and
 * argv[-2] (callee) replace null |this| with callee's parent, replace
 * primitive values with the equivalent wrapper objects and censor activation
 * objects as, per ECMA-262, they may not be referred to by |this|. argv[-1]
 * must not be a JSVAL_VOID.
 */
extern JSObject *
ComputeThisFromArgv(JSContext *cx, js::Value *argv);

JS_ALWAYS_INLINE JSObject *
ComputeThisFromVp(JSContext *cx, js::Value *vp)
{
    return ComputeThisFromArgv(cx, vp + 2);
}

JS_ALWAYS_INLINE bool
PrimitiveThisTest(JSFunction *fun, const Value &v)
{
    uint16 flags = fun->flags;
    return (v.isString() && !!(flags & JSFUN_THISP_STRING)) ||
           (v.isNumber() && !!(flags & JSFUN_THISP_NUMBER)) ||
           (v.isBoolean() && !!(flags & JSFUN_THISP_BOOLEAN));
}

/*
 * Abstracts the layout of the stack passed to natives from the engine and from
 * natives to js::Invoke.
 */
struct CallArgs
{
    Value *argv_;
    uintN argc_;
  protected:
    CallArgs() {}
    CallArgs(Value *argv, uintN argc) : argv_(argv), argc_(argc) {}
  public:
    Value *base() const { return argv_ - 2; }
    Value &callee() const { return argv_[-2]; }
    Value &thisv() const { return argv_[-1]; }
    Value &operator[](unsigned i) const { JS_ASSERT(i < argc_); return argv_[i]; }
    Value *argv() const { return argv_; }
    uintN argc() const { return argc_; }
    Value &rval() const { return argv_[-2]; }

    JSObject *computeThis(JSContext *cx) const {
        return ComputeThisFromArgv(cx, argv_);
    }
};

/*
 * The js::InvokeArgumentsGuard passed to js_Invoke must come from an
 * immediately-enclosing successful call to js::StackSpace::pushInvokeArgs,
 * i.e., there must have been no un-popped pushes to cx->stack(). Furthermore,
 * |args.getvp()[0]| should be the callee, |args.getvp()[1]| should be |this|,
 * and the range [args.getvp() + 2, args.getvp() + 2 + args.getArgc()) should
 * be initialized actual arguments.
 */
extern JS_REQUIRES_STACK bool
Invoke(JSContext *cx, const CallArgs &args, uintN flags);

/*
 * Consolidated js_Invoke flags simply rename certain JSFRAME_* flags, so that
 * we can share bits stored in JSStackFrame.flags and passed to:
 *
 *   js_Invoke
 *   js_InternalInvoke
 *   js_ValueToFunction
 *   js_ValueToFunctionObject
 *   js_ValueToCallableObject
 *   js_ReportIsNotFunction
 *
 * See jsfun.h for the latter four and flag renaming macros.
 */
#define JSINVOKE_CONSTRUCT      JSFRAME_CONSTRUCTING

/*
 * Mask to isolate construct and iterator flags for use with jsfun.h functions.
 */
#define JSINVOKE_FUNFLAGS       JSINVOKE_CONSTRUCT

/*
 * "Internal" calls may come from C or C++ code using a JSContext on which no
 * JS is running (!cx->fp), so they may need to push a dummy JSStackFrame.
 */
extern JSBool
InternalInvoke(JSContext *cx, const Value &thisv, const Value &fval, uintN flags,
               uintN argc, Value *argv, Value *rval);

static JS_ALWAYS_INLINE bool
InternalCall(JSContext *cx, JSObject *obj, const Value &fval,
             uintN argc, Value *argv, Value *rval)
{
    return InternalInvoke(cx, ObjectOrNullValue(obj), fval, 0, argc, argv, rval);
}

static JS_ALWAYS_INLINE bool
InternalConstruct(JSContext *cx, JSObject *obj, const Value &fval,
                  uintN argc, Value *argv, Value *rval)
{
    return InternalInvoke(cx, ObjectOrNullValue(obj), fval, JSINVOKE_CONSTRUCT, argc, argv, rval);
}

extern bool
InternalGetOrSet(JSContext *cx, JSObject *obj, jsid id, const Value &fval,
                 JSAccessMode mode, uintN argc, Value *argv, Value *rval);

extern JS_FORCES_STACK bool
Execute(JSContext *cx, JSObject *chain, JSScript *script,
        JSStackFrame *down, uintN flags, Value *result);

extern JS_REQUIRES_STACK bool
InvokeConstructor(JSContext *cx, const CallArgs &args);

extern JS_REQUIRES_STACK bool
Interpret(JSContext *cx);

#define JSPROP_INITIALIZER 0x100   /* NB: Not a valid property attribute. */

extern bool
CheckRedeclaration(JSContext *cx, JSObject *obj, jsid id, uintN attrs,
                   JSObject **objp, JSProperty **propp);

extern bool
StrictlyEqual(JSContext *cx, const Value &lval, const Value &rval);

/* === except that NaN is the same as NaN and -0 is not the same as +0. */
extern bool
SameValue(const Value &v1, const Value &v2, JSContext *cx);

extern JSType
TypeOfValue(JSContext *cx, const Value &v);

inline bool
InstanceOf(JSContext *cx, JSObject *obj, Class *clasp, Value *argv)
{
    if (obj && obj->getClass() == clasp)
        return true;
    extern bool InstanceOfSlow(JSContext *, JSObject *, Class *, Value *);
    return InstanceOfSlow(cx, obj, clasp, argv);
}

extern JSBool
HasInstance(JSContext *cx, JSObject *obj, const js::Value *v, JSBool *bp);

inline void *
GetInstancePrivate(JSContext *cx, JSObject *obj, Class *clasp, Value *argv)
{
    if (!InstanceOf(cx, obj, clasp, argv))
        return NULL;
    return obj->getPrivate();
}

extern bool
ValueToId(JSContext *cx, const Value &v, jsid *idp);

/*
 * @param closureLevel      The static level of the closure that the cookie
 *                          pertains to.
 * @param cookie            Level amount is a "skip" (delta) value from the
 *                          closure level.
 * @return  The value of the upvar.
 */
extern const js::Value &
GetUpvar(JSContext *cx, uintN level, js::UpvarCookie cookie);

} /* namespace js */

/*
 * JS_LONE_INTERPRET indicates that the compiler should see just the code for
 * the js_Interpret function when compiling jsinterp.cpp. The rest of the code
 * from the file should be visible only when compiling jsinvoke.cpp. It allows
 * platform builds to optimize selectively js_Interpret when the granularity
 * of the optimizations with the given compiler is a compilation unit.
 *
 * JS_STATIC_INTERPRET is the modifier for functions defined in jsinterp.cpp
 * that only js_Interpret calls. When JS_LONE_INTERPRET is true all such
 * functions are declared below.
 */
#ifndef JS_LONE_INTERPRET
# ifdef _MSC_VER
#  define JS_LONE_INTERPRET 0
# else
#  define JS_LONE_INTERPRET 1
# endif
#endif

#define JS_MAX_INLINE_CALL_COUNT 3000

#if !JS_LONE_INTERPRET
# define JS_STATIC_INTERPRET    static
#else
# define JS_STATIC_INTERPRET

extern JS_REQUIRES_STACK JSBool
js_EnterWith(JSContext *cx, jsint stackIndex);

extern JS_REQUIRES_STACK void
js_LeaveWith(JSContext *cx);

extern JS_REQUIRES_STACK js::Class *
js_IsActiveWithOrBlock(JSContext *cx, JSObject *obj, int stackDepth);

/*
 * Unwind block and scope chains to match the given depth. The function sets
 * fp->sp on return to stackDepth.
 */
extern JS_REQUIRES_STACK JSBool
js_UnwindScope(JSContext *cx, jsint stackDepth, JSBool normalUnwind);

extern JSBool
js_OnUnknownMethod(JSContext *cx, js::Value *vp);

/*
 * Find the results of incrementing or decrementing *vp. For pre-increments,
 * both *vp and *vp2 will contain the result on return. For post-increments,
 * vp will contain the original value converted to a number and vp2 will get
 * the result. Both vp and vp2 must be roots.
 */
extern JSBool
js_DoIncDec(JSContext *cx, const JSCodeSpec *cs, js::Value *vp, js::Value *vp2);

/*
 * Opcode tracing helper. When len is not 0, cx->fp->regs->pc[-len] gives the
 * previous opcode.
 */
extern JS_REQUIRES_STACK void
js_TraceOpcode(JSContext *cx);

/*
 * JS_OPMETER helper functions.
 */
extern void
js_MeterOpcodePair(JSOp op1, JSOp op2);

extern void
js_MeterSlotOpcode(JSOp op, uint32 slot);

#endif /* JS_LONE_INTERPRET */

inline JSObject *
JSStackFrame::getThisObject(JSContext *cx)
{
    JS_ASSERT(!isDummyFrame());
    if (flags & JSFRAME_COMPUTED_THIS)
        return &thisv.toObject();
    if (!js::ComputeThisFromArgv(cx, argv))
        return NULL;
    setThisValue(argv[-1]);
    flags |= JSFRAME_COMPUTED_THIS;
    return &thisv.toObject();
}

#endif /* jsinterp_h___ */
