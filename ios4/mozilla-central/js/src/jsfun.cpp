/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sw=4 et tw=99:
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

/*
 * JS function support.
 */
#include <string.h>
#include "jstypes.h"
#include "jsstdint.h"
#include "jsbit.h"
#include "jsutil.h" /* Added by JSIFY */
#include "jsapi.h"
#include "jsarray.h"
#include "jsatom.h"
#include "jsbool.h"
#include "jsbuiltins.h"
#include "jscntxt.h"
#include "jsversion.h"
#include "jsdbgapi.h"
#include "jsemit.h"
#include "jsfun.h"
#include "jsgc.h"
#include "jsinterp.h"
#include "jslock.h"
#include "jsnum.h"
#include "jsobj.h"
#include "jsopcode.h"
#include "jsparse.h"
#include "jsproxy.h"
#include "jsscan.h"
#include "jsscope.h"
#include "jsscript.h"
#include "jsstr.h"
#include "jsexn.h"
#include "jsstaticcheck.h"
#include "jstracer.h"

#if JS_HAS_GENERATORS
# include "jsiter.h"
#endif

#if JS_HAS_XDR
# include "jsxdrapi.h"
#endif

#include "jsatominlines.h"
#include "jscntxtinlines.h"
#include "jsfuninlines.h"
#include "jsobjinlines.h"
#include "jscntxtinlines.h"

using namespace js;

inline JSObject *
JSObject::getThrowTypeError() const
{
    return &getGlobal()->getReservedSlot(JSRESERVED_GLOBAL_THROWTYPEERROR).toObject();
}

JSBool
js_GetArgsValue(JSContext *cx, JSStackFrame *fp, Value *vp)
{
    JSObject *argsobj;

    if (fp->flags & JSFRAME_OVERRIDE_ARGS) {
        JS_ASSERT(fp->hasCallObj());
        jsid id = ATOM_TO_JSID(cx->runtime->atomState.argumentsAtom);
        return fp->getCallObj()->getProperty(cx, id, vp);
    }
    argsobj = js_GetArgsObject(cx, fp);
    if (!argsobj)
        return JS_FALSE;
    vp->setObject(*argsobj);
    return JS_TRUE;
}

JSBool
js_GetArgsProperty(JSContext *cx, JSStackFrame *fp, jsid id, Value *vp)
{
    if (fp->flags & JSFRAME_OVERRIDE_ARGS) {
        JS_ASSERT(fp->hasCallObj());

        jsid argumentsid = ATOM_TO_JSID(cx->runtime->atomState.argumentsAtom);
        Value v;
        if (!fp->getCallObj()->getProperty(cx, argumentsid, &v))
            return false;

        JSObject *obj;
        if (v.isPrimitive()) {
            obj = js_ValueToNonNullObject(cx, v);
            if (!obj)
                return false;
        } else {
            obj = &v.toObject();
        }
        return obj->getProperty(cx, id, vp);
    }

    vp->setUndefined();
    if (JSID_IS_INT(id)) {
        uint32 arg = uint32(JSID_TO_INT(id));
        JSObject *argsobj = fp->maybeArgsObj();
        if (arg < fp->numActualArgs()) {
            if (argsobj) {
                if (argsobj->getArgsElement(arg).isMagic(JS_ARGS_HOLE))
                    return argsobj->getProperty(cx, id, vp);
            }
            *vp = fp->argv[arg];
        } else {
            /*
             * Per ECMA-262 Ed. 3, 10.1.8, last bulleted item, do not share
             * storage between the formal parameter and arguments[k] for all
             * fp->argc <= k && k < fp->fun->nargs.  For example, in
             *
             *   function f(x) { x = 42; return arguments[0]; }
             *   f();
             *
             * the call to f should return undefined, not 42.  If fp->argsobj
             * is null at this point, as it would be in the example, return
             * undefined in *vp.
             */
            if (argsobj)
                return argsobj->getProperty(cx, id, vp);
        }
    } else if (JSID_IS_ATOM(id, cx->runtime->atomState.lengthAtom)) {
        JSObject *argsobj = fp->maybeArgsObj();
        if (argsobj && argsobj->isArgsLengthOverridden())
            return argsobj->getProperty(cx, id, vp);
        vp->setInt32(fp->numActualArgs());
    }
    return true;
}

static JSObject *
NewArguments(JSContext *cx, JSObject *parent, uint32 argc, JSObject *callee)
{
    JSObject *proto;
    if (!js_GetClassPrototype(cx, parent, JSProto_Object, &proto))
        return NULL;

    JSObject *argsobj = js_NewGCObject(cx);
    if (!argsobj)
        return NULL;

    /* Init immediately to avoid GC seeing a half-init'ed object. */
    bool strict = callee->getFunctionPrivate()->inStrictMode();
    argsobj->init(strict ? &StrictArgumentsClass : &js_ArgumentsClass, proto, parent,
                  PrivateValue(NULL));
    argsobj->setArgsLength(argc);
    argsobj->setArgsCallee(ObjectValue(*callee));
    argsobj->map = cx->runtime->emptyArgumentsScope->hold();

    /* This must come after argsobj->map has been set. */
    if (!js_EnsureReservedSlots(cx, argsobj, argc))
        return NULL;

    return argsobj;
}

static void
PutArguments(JSContext *cx, JSObject *argsobj, Value *args)
{
    JS_ASSERT(argsobj->isNormalArguments());
    uint32 argc = argsobj->getArgsInitialLength();
    for (uint32 i = 0; i != argc; ++i) {
        if (!argsobj->getArgsElement(i).isMagic(JS_ARGS_HOLE))
            argsobj->setArgsElement(i, args[i]);
    }
}

JSObject *
js_GetArgsObject(JSContext *cx, JSStackFrame *fp)
{
    /*
     * We must be in a function activation; the function must be lightweight
     * or else fp must have a variable object.
     */
    JS_ASSERT(fp->hasFunction());
    JS_ASSERT_IF(fp->getFunction()->flags & JSFUN_HEAVYWEIGHT,
                 fp->varobj(cx->containingSegment(fp)));

    /* Skip eval and debugger frames. */
    while (fp->flags & JSFRAME_SPECIAL)
        fp = fp->down;

    /* Create an arguments object for fp only if it lacks one. */
    if (fp->hasArgsObj())
        return fp->getArgsObj();

    /* Compute the arguments object's parent slot from fp's scope chain. */
    JSObject *global = fp->getScopeChain()->getGlobal();
    JSObject *argsobj = NewArguments(cx, global, fp->numActualArgs(), &fp->argv[-2].toObject());
    if (!argsobj)
        return argsobj;

    /*
     * Strict mode functions have arguments which copy the initial parameter
     * values.  It is the caller's responsibility to get the arguments object
     * before any parameters are modified!  (The emitter ensures this by
     * synthesizing an arguments access at the start of any strict mode
     * function which contains an assignment to a parameter or which calls
     * eval.)  Non-strict mode arguments use the frame pointer to retrieve
     * up-to-date parameter values.
     */
    if (argsobj->isStrictArguments()) {
        JS_ASSERT_IF(fp->numActualArgs() > 0,
                     argsobj->dslots[-1].toPrivateUint32() >= fp->numActualArgs());
        memcpy(argsobj->dslots, fp->argv, fp->numActualArgs() * sizeof(Value));
    } else {
        argsobj->setPrivate(fp);
    }

    fp->setArgsObj(argsobj);
    return argsobj;
}

void
js_PutArgsObject(JSContext *cx, JSStackFrame *fp)
{
    JSObject *argsobj = fp->getArgsObj();
    if (argsobj->isNormalArguments()) {
        JS_ASSERT(argsobj->getPrivate() == fp);
        PutArguments(cx, argsobj, fp->argv);
        argsobj->setPrivate(NULL);
    } else {
        JS_ASSERT(!argsobj->getPrivate());
    }
    fp->setArgsObj(NULL);
}

/*
 * Traced versions of js_GetArgsObject and js_PutArgsObject.
 */

#ifdef JS_TRACER
JSObject * JS_FASTCALL
js_Arguments(JSContext *cx, JSObject *parent, uint32 argc, JSObject *callee)
{
    JSObject *argsobj = NewArguments(cx, parent, argc, callee);
    if (!argsobj)
        return NULL;

    if (callee->getFunctionPrivate()->inStrictMode()) {
        /*
         * Strict mode callers must copy arguments into the created arguments
         * object.
         */
        JS_ASSERT(!argsobj->getPrivate());
    } else {
        argsobj->setPrivate(JS_ARGUMENT_OBJECT_ON_TRACE);
    }

    return argsobj;
}
#endif

JS_DEFINE_CALLINFO_4(extern, OBJECT, js_Arguments, CONTEXT, OBJECT, UINT32, OBJECT,
                     0, nanojit::ACCSET_STORE_ANY)

/* FIXME change the return type to void. */
JSBool JS_FASTCALL
js_PutArguments(JSContext *cx, JSObject *argsobj, Value *args)
{
    JS_ASSERT(argsobj->isNormalArguments());
    JS_ASSERT(argsobj->getPrivate() == JS_ARGUMENT_OBJECT_ON_TRACE);
    PutArguments(cx, argsobj, args);
    argsobj->setPrivate(NULL);
    return true;
}

JS_DEFINE_CALLINFO_3(extern, BOOL, js_PutArguments, CONTEXT, OBJECT, VALUEPTR, 0,
                     nanojit::ACCSET_STORE_ANY)

static JSBool
args_delProperty(JSContext *cx, JSObject *obj, jsid id, Value *vp)
{
    JS_ASSERT(obj->isArguments());

    if (JSID_IS_INT(id)) {
        uintN arg = uintN(JSID_TO_INT(id));
        if (arg < obj->getArgsInitialLength())
            obj->setArgsElement(arg, MagicValue(JS_ARGS_HOLE));
    } else if (JSID_IS_ATOM(id, cx->runtime->atomState.lengthAtom)) {
        obj->setArgsLengthOverridden();
    } else if (JSID_IS_ATOM(id, cx->runtime->atomState.calleeAtom)) {
        obj->setArgsCallee(MagicValue(JS_ARGS_HOLE));
    }
    return true;
}

static JS_REQUIRES_STACK JSObject *
WrapEscapingClosure(JSContext *cx, JSStackFrame *fp, JSFunction *fun)
{
    JS_ASSERT(fun->optimizedClosure());
    JS_ASSERT(!fun->u.i.wrapper);

    /*
     * We do not attempt to reify Call and Block objects on demand for outer
     * scopes. This could be done (see the "v8" patch in bug 494235) but it is
     * fragile in the face of ongoing compile-time optimization. Instead, the
     * _DBG* opcodes used by wrappers created here must cope with unresolved
     * upvars and throw them as reference errors. Caveat debuggers!
     */
    JSObject *scopeChain = js_GetScopeChain(cx, fp);
    if (!scopeChain)
        return NULL;

    JSObject *wfunobj = NewFunction(cx, scopeChain);
    if (!wfunobj)
        return NULL;
    AutoObjectRooter tvr(cx, wfunobj);

    JSFunction *wfun = (JSFunction *) wfunobj;
    wfunobj->setPrivate(wfun);
    wfun->nargs = 0;
    wfun->flags = fun->flags | JSFUN_HEAVYWEIGHT;
    wfun->u.i.nvars = 0;
    wfun->u.i.nupvars = 0;
    wfun->u.i.skipmin = fun->u.i.skipmin;
    wfun->u.i.wrapper = true;
    wfun->u.i.script = NULL;
    wfun->u.i.names.taggedAtom = NULL;
    wfun->atom = fun->atom;

    if (fun->hasLocalNames()) {
        void *mark = JS_ARENA_MARK(&cx->tempPool);
        jsuword *names = js_GetLocalNameArray(cx, fun, &cx->tempPool);
        if (!names)
            return NULL;

        JSBool ok = true;
        for (uintN i = 0, n = fun->countLocalNames(); i != n; i++) {
            jsuword name = names[i];
            JSAtom *atom = JS_LOCAL_NAME_TO_ATOM(name);
            JSLocalKind localKind = (i < fun->nargs)
                                    ? JSLOCAL_ARG
                                    : (i < fun->countArgsAndVars())
                                    ? (JS_LOCAL_NAME_IS_CONST(name)
                                       ? JSLOCAL_CONST
                                       : JSLOCAL_VAR)
                                    : JSLOCAL_UPVAR;

            ok = js_AddLocal(cx, wfun, atom, localKind);
            if (!ok)
                break;
        }

        JS_ARENA_RELEASE(&cx->tempPool, mark);
        if (!ok)
            return NULL;
        JS_ASSERT(wfun->nargs == fun->nargs);
        JS_ASSERT(wfun->u.i.nvars == fun->u.i.nvars);
        JS_ASSERT(wfun->u.i.nupvars == fun->u.i.nupvars);
        js_FreezeLocalNames(cx, wfun);
    }

    JSScript *script = fun->u.i.script;
    jssrcnote *snbase = script->notes();
    jssrcnote *sn = snbase;
    while (!SN_IS_TERMINATOR(sn))
        sn = SN_NEXT(sn);
    uintN nsrcnotes = (sn - snbase) + 1;

    /* NB: GC must not occur before wscript is homed in wfun->u.i.script. */
    JSScript *wscript = js_NewScript(cx, script->length, nsrcnotes,
                                     script->atomMap.length,
                                     (script->objectsOffset != 0)
                                     ? script->objects()->length
                                     : 0,
                                     fun->u.i.nupvars,
                                     (script->regexpsOffset != 0)
                                     ? script->regexps()->length
                                     : 0,
                                     (script->trynotesOffset != 0)
                                     ? script->trynotes()->length
                                     : 0,
                                     (script->constOffset != 0)
                                     ? script->consts()->length
                                     : 0);
    if (!wscript)
        return NULL;

    memcpy(wscript->code, script->code, script->length);
    wscript->main = wscript->code + (script->main - script->code);

    memcpy(wscript->notes(), snbase, nsrcnotes * sizeof(jssrcnote));
    memcpy(wscript->atomMap.vector, script->atomMap.vector,
           wscript->atomMap.length * sizeof(JSAtom *));
    if (script->objectsOffset != 0) {
        memcpy(wscript->objects()->vector, script->objects()->vector,
               wscript->objects()->length * sizeof(JSObject *));
    }
    if (script->regexpsOffset != 0) {
        memcpy(wscript->regexps()->vector, script->regexps()->vector,
               wscript->regexps()->length * sizeof(JSObject *));
    }
    if (script->trynotesOffset != 0) {
        memcpy(wscript->trynotes()->vector, script->trynotes()->vector,
               wscript->trynotes()->length * sizeof(JSTryNote));
    }

    if (wfun->u.i.nupvars != 0) {
        JS_ASSERT(wfun->u.i.nupvars == wscript->upvars()->length);
        memcpy(wscript->upvars()->vector, script->upvars()->vector,
               wfun->u.i.nupvars * sizeof(uint32));
    }

    jsbytecode *pc = wscript->code;
    while (*pc != JSOP_STOP) {
        /* XYZZYbe should copy JSOP_TRAP? */
        JSOp op = js_GetOpcode(cx, wscript, pc);
        const JSCodeSpec *cs = &js_CodeSpec[op];
        ptrdiff_t oplen = cs->length;
        if (oplen < 0)
            oplen = js_GetVariableBytecodeLength(pc);

        /*
         * Rewrite JSOP_{GET,CALL}DSLOT as JSOP_{GET,CALL}UPVAR_DBG for the
         * case where fun is an escaping flat closure. This works because the
         * UPVAR and DSLOT ops by design have the same format: an upvar index
         * immediate operand.
         */
        switch (op) {
          case JSOP_GETUPVAR:       *pc = JSOP_GETUPVAR_DBG; break;
          case JSOP_CALLUPVAR:      *pc = JSOP_CALLUPVAR_DBG; break;
          case JSOP_GETDSLOT:       *pc = JSOP_GETUPVAR_DBG; break;
          case JSOP_CALLDSLOT:      *pc = JSOP_CALLUPVAR_DBG; break;
          case JSOP_DEFFUN_FC:      *pc = JSOP_DEFFUN_DBGFC; break;
          case JSOP_DEFLOCALFUN_FC: *pc = JSOP_DEFLOCALFUN_DBGFC; break;
          case JSOP_LAMBDA_FC:      *pc = JSOP_LAMBDA_DBGFC; break;
          default:;
        }
        pc += oplen;
    }

    /*
     * Fill in the rest of wscript. This means if you add members to JSScript
     * you must update this code. FIXME: factor into JSScript::clone method.
     */
    wscript->noScriptRval = script->noScriptRval;
    wscript->savedCallerFun = script->savedCallerFun;
    wscript->hasSharps = script->hasSharps;
    wscript->strictModeCode = script->strictModeCode;
    wscript->version = script->version;
    wscript->nfixed = script->nfixed;
    wscript->filename = script->filename;
    wscript->lineno = script->lineno;
    wscript->nslots = script->nslots;
    wscript->staticLevel = script->staticLevel;
    wscript->principals = script->principals;
    if (wscript->principals)
        JSPRINCIPALS_HOLD(cx, wscript->principals);
#ifdef CHECK_SCRIPT_OWNER
    wscript->owner = script->owner;
#endif

    /* Deoptimize wfun from FUN_{FLAT,NULL}_CLOSURE to FUN_INTERPRETED. */
    FUN_SET_KIND(wfun, JSFUN_INTERPRETED);
    wfun->u.i.script = wscript;
    return wfunobj;
}

static JSBool
ArgGetter(JSContext *cx, JSObject *obj, jsid id, Value *vp)
{
    LeaveTrace(cx);

    if (!InstanceOf(cx, obj, &js_ArgumentsClass, NULL))
        return true;

    if (JSID_IS_INT(id)) {
        /*
         * arg can exceed the number of arguments if a script changed the
         * prototype to point to another Arguments object with a bigger argc.
         */
        uintN arg = uintN(JSID_TO_INT(id));
        if (arg < obj->getArgsInitialLength()) {
            JSStackFrame *fp = (JSStackFrame *) obj->getPrivate();
            if (fp) {
                *vp = fp->argv[arg];
            } else {
                const Value &v = obj->getArgsElement(arg);
                if (!v.isMagic(JS_ARGS_HOLE))
                    *vp = v;
            }
        }
    } else if (JSID_IS_ATOM(id, cx->runtime->atomState.lengthAtom)) {
        if (!obj->isArgsLengthOverridden())
            vp->setInt32(obj->getArgsInitialLength());
    } else {
        JS_ASSERT(JSID_IS_ATOM(id, cx->runtime->atomState.calleeAtom));
        const Value &v = obj->getArgsCallee();
        if (!v.isMagic(JS_ARGS_HOLE)) {
            /*
             * If this function or one in it needs upvars that reach above it
             * in the scope chain, it must not be a null closure (it could be a
             * flat closure, or an unoptimized closure -- the latter itself not
             * necessarily heavyweight). Rather than wrap here, we simply throw
             * to reduce code size and tell debugger users the truth instead of
             * passing off a fibbing wrapper.
             */
            if (GET_FUNCTION_PRIVATE(cx, &v.toObject())->needsWrapper()) {
                JS_ReportErrorNumber(cx, js_GetErrorMessage, NULL,
                                     JSMSG_OPTIMIZED_CLOSURE_LEAK);
                return false;
            }
            *vp = v;
        }
    }
    return true;
}

static JSBool
ArgSetter(JSContext *cx, JSObject *obj, jsid id, Value *vp)
{
#ifdef JS_TRACER
    // To be able to set a property here on trace, we would have to make
    // sure any updates also get written back to the trace native stack.
    // For simplicity, we just leave trace, since this is presumably not
    // a common operation.
    if (JS_ON_TRACE(cx)) {
        DeepBail(cx);
        return false;
    }
#endif

    if (!InstanceOf(cx, obj, &js_ArgumentsClass, NULL))
        return true;

    if (JSID_IS_INT(id)) {
        uintN arg = uintN(JSID_TO_INT(id));
        if (arg < obj->getArgsInitialLength()) {
            JSStackFrame *fp = (JSStackFrame *) obj->getPrivate();
            if (fp) {
                fp->argv[arg] = *vp;
                return true;
            }
        }
    } else {
        JS_ASSERT(JSID_IS_ATOM(id, cx->runtime->atomState.lengthAtom) ||
                  JSID_IS_ATOM(id, cx->runtime->atomState.calleeAtom));
    }

    /*
     * For simplicity we use delete/set to replace the property with one
     * backed by the default Object getter and setter. Note that we rely on
     * args_delProperty to clear the corresponding reserved slot so the GC can
     * collect its value.
     */
    AutoValueRooter tvr(cx);
    return js_DeleteProperty(cx, obj, id, tvr.addr()) &&
           js_SetProperty(cx, obj, id, vp);
}

static JSBool
args_resolve(JSContext *cx, JSObject *obj, jsid id, uintN flags,
             JSObject **objp)
{
    JS_ASSERT(obj->isNormalArguments());

    *objp = NULL;
    bool valid = false;
    uintN attrs = JSPROP_SHARED;
    if (JSID_IS_INT(id)) {
        uint32 arg = uint32(JSID_TO_INT(id));
        attrs = JSPROP_ENUMERATE | JSPROP_SHARED;
        if (arg < obj->getArgsInitialLength() && !obj->getArgsElement(arg).isMagic(JS_ARGS_HOLE))
            valid = true;
    } else if (JSID_IS_ATOM(id, cx->runtime->atomState.lengthAtom)) {
        if (!obj->isArgsLengthOverridden())
            valid = true;
    } else if (JSID_IS_ATOM(id, cx->runtime->atomState.calleeAtom)) {
        if (!obj->getArgsCallee().isMagic(JS_ARGS_HOLE))
            valid = true;
    }

    if (valid) {
        Value tmp = UndefinedValue();
        if (!js_DefineProperty(cx, obj, id, &tmp, ArgGetter, ArgSetter, attrs))
            return JS_FALSE;
        *objp = obj;
    }
    return true;
}

static JSBool
args_enumerate(JSContext *cx, JSObject *obj)
{
    JS_ASSERT(obj->isNormalArguments());

    /*
     * Trigger reflection in args_resolve using a series of js_LookupProperty
     * calls.
     */
    int argc = int(obj->getArgsInitialLength());
    for (int i = -2; i != argc; i++) {
        jsid id = (i == -2)
                  ? ATOM_TO_JSID(cx->runtime->atomState.lengthAtom)
                  : (i == -1)
                  ? ATOM_TO_JSID(cx->runtime->atomState.calleeAtom)
                  : INT_TO_JSID(i);

        JSObject *pobj;
        JSProperty *prop;
        if (!js_LookupProperty(cx, obj, id, &pobj, &prop))
            return false;

        /* prop is null when the property was deleted. */
        if (prop)
            pobj->dropProperty(cx, prop);
    }
    return true;
}

namespace {

JSBool
StrictArgGetter(JSContext *cx, JSObject *obj, jsid id, Value *vp)
{
    LeaveTrace(cx);

    if (!InstanceOf(cx, obj, &StrictArgumentsClass, NULL))
        return true;

    if (JSID_IS_INT(id)) {
        /*
         * arg can exceed the number of arguments if a script changed the
         * prototype to point to another Arguments object with a bigger argc.
         */
        uintN arg = uintN(JSID_TO_INT(id));
        if (arg < obj->getArgsInitialLength()) {
            const Value &v = obj->getArgsElement(arg);
            if (!v.isMagic(JS_ARGS_HOLE))
                *vp = v;
        }
    } else {
        JS_ASSERT(JSID_IS_ATOM(id, cx->runtime->atomState.lengthAtom));
        if (!obj->isArgsLengthOverridden())
            vp->setInt32(obj->getArgsInitialLength());
    }
    return true;
}

JSBool
StrictArgSetter(JSContext *cx, JSObject *obj, jsid id, Value *vp)
{
    if (!InstanceOf(cx, obj, &StrictArgumentsClass, NULL))
        return true;

    if (JSID_IS_INT(id)) {
        uintN arg = uintN(JSID_TO_INT(id));
        if (arg < obj->getArgsInitialLength()) {
            obj->setArgsElement(arg, *vp);
            return true;
        }
    } else {
        JS_ASSERT(JSID_IS_ATOM(id, cx->runtime->atomState.lengthAtom));
    }

    /*
     * For simplicity we use delete/set to replace the property with one
     * backed by the default Object getter and setter. Note that we rely on
     * args_delProperty to clear the corresponding reserved slot so the GC can
     * collect its value.
     */
    AutoValueRooter tvr(cx);
    return js_DeleteProperty(cx, obj, id, tvr.addr()) &&
           js_SetProperty(cx, obj, id, vp);
}

JSBool
strictargs_resolve(JSContext *cx, JSObject *obj, jsid id, uintN flags, JSObject **objp)
{
    JS_ASSERT(obj->isStrictArguments());

    *objp = NULL;
    bool valid = false;
    uintN attrs = JSPROP_SHARED;
    if (JSID_IS_INT(id)) {
        uint32 arg = uint32(JSID_TO_INT(id));
        attrs = JSPROP_SHARED | JSPROP_ENUMERATE;
        if (arg < obj->getArgsInitialLength() && !obj->getArgsElement(arg).isMagic(JS_ARGS_HOLE))
            valid = true;
    } else if (JSID_IS_ATOM(id, cx->runtime->atomState.lengthAtom)) {
        if (!obj->isArgsLengthOverridden())
            valid = true;
    } else if (JSID_IS_ATOM(id, cx->runtime->atomState.calleeAtom)) {
        Value tmp = UndefinedValue();
        PropertyOp throwTypeError = CastAsPropertyOp(obj->getThrowTypeError());
        uintN attrs = JSPROP_PERMANENT | JSPROP_GETTER | JSPROP_SETTER | JSPROP_SHARED;
        if (!js_DefineProperty(cx, obj, id, &tmp, throwTypeError, throwTypeError, attrs))
            return false;

        *objp = obj;
        return true;
    } else if (JSID_IS_ATOM(id, cx->runtime->atomState.callerAtom)) {
        /*
         * Strict mode arguments objects have an immutable poison-pill caller
         * property that throws a TypeError on getting or setting.
         */
        PropertyOp throwTypeError = CastAsPropertyOp(obj->getThrowTypeError());
        Value tmp = UndefinedValue();
        if (!js_DefineProperty(cx, obj, id, &tmp, throwTypeError, throwTypeError,
                               JSPROP_PERMANENT | JSPROP_GETTER | JSPROP_SETTER | JSPROP_SHARED)) {
            return false;
        }

        *objp = obj;
        return true;
    }

    if (valid) {
        Value tmp = UndefinedValue();
        if (!js_DefineProperty(cx, obj, id, &tmp, StrictArgGetter, StrictArgSetter, attrs))
            return false;
        *objp = obj;
    }
    return true;
}

JSBool
strictargs_enumerate(JSContext *cx, JSObject *obj)
{
    JS_ASSERT(obj->isStrictArguments());

    /*
     * Trigger reflection in strictargs_resolve using a series of
     * js_LookupProperty calls.  Beware deleted properties!
     */
    JSObject *pobj;
    JSProperty *prop;

    // length
    if (!js_LookupProperty(cx, obj, ATOM_TO_JSID(cx->runtime->atomState.lengthAtom), &pobj, &prop))
        return false;
    if (prop)
        pobj->dropProperty(cx, prop);

    // callee
    if (!js_LookupProperty(cx, obj, ATOM_TO_JSID(cx->runtime->atomState.calleeAtom), &pobj, &prop))
        return false;
    if (prop)
        pobj->dropProperty(cx, prop);

    // caller
    if (!js_LookupProperty(cx, obj, ATOM_TO_JSID(cx->runtime->atomState.callerAtom), &pobj, &prop))
        return false;
    if (prop)
        pobj->dropProperty(cx, prop);

    for (uint32 i = 0, argc = obj->getArgsInitialLength(); i < argc; i++) {
        if (!js_LookupProperty(cx, obj, INT_TO_JSID(i), &pobj, &prop))
            return false;
        if (prop)
            pobj->dropProperty(cx, prop);
    }

    return true;
}

} // namespace

#if JS_HAS_GENERATORS
/*
 * If a generator's arguments or call object escapes, and the generator frame
 * is not executing, the generator object needs to be marked because it is not
 * otherwise reachable. An executing generator is rooted by its invocation.  To
 * distinguish the two cases (which imply different access paths to the
 * generator object), we use the JSFRAME_FLOATING_GENERATOR flag, which is only
 * set on the JSStackFrame kept in the generator object's JSGenerator.
 */
static void
args_or_call_trace(JSTracer *trc, JSObject *obj)
{
    if (obj->isArguments()) {
        if (obj->getPrivate() == JS_ARGUMENT_OBJECT_ON_TRACE)
            return;
    } else {
        JS_ASSERT(obj->getClass() == &js_CallClass);
    }

    JSStackFrame *fp = (JSStackFrame *) obj->getPrivate();
    if (fp && fp->isFloatingGenerator()) {
        JSObject *obj = js_FloatingFrameToGenerator(fp)->obj;
        JS_CALL_OBJECT_TRACER(trc, obj, "generator object");
    }
}
#else
# define args_or_call_trace NULL
#endif

/*
 * The Arguments classes aren't initialized via JS_InitClass, because arguments
 * objects have the initial value of Object.prototype as their [[Prototype]].
 * However, Object.prototype.toString.call(arguments) === "[object Arguments]"
 * per ES5 (although not ES3), so the class name is "Arguments" rather than
 * "Object".
 */

/*
 *
 * The JSClass functions below collaborate to lazily reflect and synchronize
 * actual argument values, argument count, and callee function object stored
 * in a JSStackFrame with their corresponding property values in the frame's
 * arguments object.
 */
Class js_ArgumentsClass = {
    "Arguments",
    JSCLASS_HAS_PRIVATE | JSCLASS_NEW_RESOLVE |
    JSCLASS_HAS_RESERVED_SLOTS(JSObject::ARGS_FIXED_RESERVED_SLOTS) |
    JSCLASS_MARK_IS_TRACE | JSCLASS_HAS_CACHED_PROTO(JSProto_Object),
    PropertyStub,   /* addProperty */
    args_delProperty,
    PropertyStub,   /* getProperty */
    PropertyStub,   /* setProperty */
    args_enumerate,
    (JSResolveOp) args_resolve,
    ConvertStub,
    NULL,           /* finalize   */
    NULL,           /* reserved0   */
    NULL,           /* checkAccess */
    NULL,           /* call        */
    NULL,           /* construct   */
    NULL,           /* xdrObject   */
    NULL,           /* hasInstance */
    JS_CLASS_TRACE(args_or_call_trace)
};

namespace js {

/*
 * Strict mode arguments is significantly less magical than non-strict mode
 * arguments, so it is represented by a different class while sharing some
 * functionality.
 */
Class StrictArgumentsClass = {
    "Arguments",
    JSCLASS_HAS_PRIVATE | JSCLASS_NEW_RESOLVE |
    JSCLASS_HAS_RESERVED_SLOTS(JSObject::ARGS_FIXED_RESERVED_SLOTS) |
    JSCLASS_MARK_IS_TRACE | JSCLASS_HAS_CACHED_PROTO(JSProto_Object),
    PropertyStub,   /* addProperty */
    args_delProperty,
    PropertyStub,   /* getProperty */
    PropertyStub,   /* setProperty */
    strictargs_enumerate,
    reinterpret_cast<JSResolveOp>(strictargs_resolve),
    ConvertStub,
    NULL,           /* finalize   */
    NULL,           /* reserved0   */
    NULL,           /* checkAccess */
    NULL,           /* call        */
    NULL,           /* construct   */
    NULL,           /* xdrObject   */
    NULL,           /* hasInstance */
    JS_CLASS_TRACE(args_or_call_trace)
};

}

const uint32 JSSLOT_CALLEE =                    JSSLOT_PRIVATE + 1;
const uint32 JSSLOT_CALL_ARGUMENTS =            JSSLOT_PRIVATE + 2;
const uint32 CALL_CLASS_FIXED_RESERVED_SLOTS =  2;

/*
 * A Declarative Environment object stores its active JSStackFrame pointer in
 * its private slot, just as Call and Arguments objects do.
 */
Class js_DeclEnvClass = {
    js_Object_str,
    JSCLASS_HAS_PRIVATE | JSCLASS_HAS_CACHED_PROTO(JSProto_Object),
    PropertyStub,   /* addProperty */
    PropertyStub,   /* delProperty */
    PropertyStub,   /* getProperty */
    PropertyStub,   /* setProperty */
    EnumerateStub,
    ResolveStub,
    ConvertStub
};

static JSBool
CheckForEscapingClosure(JSContext *cx, JSObject *obj, Value *vp)
{
    JS_ASSERT(obj->getClass() == &js_CallClass ||
              obj->getClass() == &js_DeclEnvClass);

    const Value &v = *vp;

    JSObject *funobj;
    if (IsFunctionObject(v, &funobj)) {
        JSFunction *fun = GET_FUNCTION_PRIVATE(cx, funobj);

        /*
         * Any escaping null or flat closure that reaches above itself or
         * contains nested functions that reach above it must be wrapped.
         * We can wrap only when this Call or Declarative Environment obj
         * still has an active stack frame associated with it.
         */
        if (fun->needsWrapper()) {
            LeaveTrace(cx);

            JSStackFrame *fp = (JSStackFrame *) obj->getPrivate();
            if (fp) {
                JSObject *wrapper = WrapEscapingClosure(cx, fp, fun);
                if (!wrapper)
                    return false;
                vp->setObject(*wrapper);
                return true;
            }

            JS_ReportErrorNumber(cx, js_GetErrorMessage, NULL,
                                 JSMSG_OPTIMIZED_CLOSURE_LEAK);
            return false;
        }
    }
    return true;
}

static JSBool
CalleeGetter(JSContext *cx, JSObject *obj, jsid id, Value *vp)
{
    return CheckForEscapingClosure(cx, obj, vp);
}

static JSObject *
NewCallObject(JSContext *cx, JSFunction *fun, JSObject *scopeChain)
{
    JSObject *callobj = js_NewGCObject(cx);
    if (!callobj)
        return NULL;

    /* Init immediately to avoid GC seeing a half-init'ed object. */
    callobj->init(&js_CallClass, NULL, scopeChain, PrivateValue(NULL));
    callobj->map = cx->runtime->emptyCallScope->hold();

    /* This must come after callobj->map has been set. */
    if (!js_EnsureReservedSlots(cx, callobj, fun->countArgsAndVars()))
        return NULL;
    return callobj;
}

static inline JSObject *
NewDeclEnvObject(JSContext *cx, JSStackFrame *fp)
{
    JSObject *envobj = js_NewGCObject(cx);
    if (!envobj)
        return NULL;

    /* Init immediately to avoid GC seeing a half-init'ed object. */
    envobj->init(&js_DeclEnvClass, NULL, fp->maybeScopeChain(), PrivateValue(fp));
    envobj->map = cx->runtime->emptyDeclEnvScope->hold();
    return envobj;
}

JSObject *
js_GetCallObject(JSContext *cx, JSStackFrame *fp)
{
    /* Create a call object for fp only if it lacks one. */
    JS_ASSERT(fp->hasFunction());
    if (fp->hasCallObj())
        return fp->getCallObj();

#ifdef DEBUG
    /* A call object should be a frame's outermost scope chain element.  */
    Class *classp = fp->getScopeChain()->getClass();
    if (classp == &js_WithClass || classp == &js_BlockClass)
        JS_ASSERT(fp->getScopeChain()->getPrivate() != js_FloatingFrameIfGenerator(cx, fp));
    else if (classp == &js_CallClass)
        JS_ASSERT(fp->getScopeChain()->getPrivate() != fp);
#endif

    /*
     * Create the call object, using the frame's enclosing scope as its
     * parent, and link the call to its stack frame. For a named function
     * expression Call's parent points to an environment object holding
     * function's name.
     */
    JSAtom *lambdaName =
        (fp->getFunction()->flags & JSFUN_LAMBDA) ? fp->getFunction()->atom : NULL;
    if (lambdaName) {
        JSObject *envobj = NewDeclEnvObject(cx, fp);
        if (!envobj)
            return NULL;

        /* Root envobj before js_DefineNativeProperty (-> JSClass.addProperty). */
        fp->setScopeChain(envobj);
        JS_ASSERT(fp->argv);
        if (!js_DefineNativeProperty(cx, fp->getScopeChain(), ATOM_TO_JSID(lambdaName),
                                     fp->calleeValue(),
                                     CalleeGetter, NULL,
                                     JSPROP_PERMANENT | JSPROP_READONLY,
                                     0, 0, NULL)) {
            return NULL;
        }
    }

    JSObject *callobj = NewCallObject(cx, fp->getFunction(), fp->getScopeChain());
    if (!callobj)
        return NULL;

    callobj->setPrivate(fp);
    JS_ASSERT(fp->argv);
    JS_ASSERT(fp->getFunction() == GET_FUNCTION_PRIVATE(cx, fp->callee()));
    callobj->setSlot(JSSLOT_CALLEE, fp->calleeValue());
    fp->setCallObj(callobj);

    /*
     * Push callobj on the top of the scope chain, and make it the
     * variables object.
     */
    fp->setScopeChain(callobj);
    return callobj;
}

JSObject * JS_FASTCALL
js_CreateCallObjectOnTrace(JSContext *cx, JSFunction *fun, JSObject *callee, JSObject *scopeChain)
{
    JS_ASSERT(!js_IsNamedLambda(fun));
    JSObject *callobj = NewCallObject(cx, fun, scopeChain);
    if (!callobj)
        return NULL;
    callobj->setSlot(JSSLOT_CALLEE, ObjectValue(*callee));
    return callobj;
}

JS_DEFINE_CALLINFO_4(extern, OBJECT, js_CreateCallObjectOnTrace, CONTEXT, FUNCTION, OBJECT, OBJECT,
                     0, nanojit::ACCSET_STORE_ANY)

JSFunction *
js_GetCallObjectFunction(JSObject *obj)
{
    JS_ASSERT(obj->getClass() == &js_CallClass);
    const Value &v = obj->getSlot(JSSLOT_CALLEE);
    if (v.isUndefined()) {
        /* Newborn or prototype object. */
        return NULL;
    }
    JS_ASSERT(v.isObject());
    return GET_FUNCTION_PRIVATE(cx, &v.toObject());
}

inline static void
CopyValuesToCallObject(JSObject *callobj, int nargs, Value *argv, int nvars, Value *slots)
{
    memcpy(callobj->dslots, argv, nargs * sizeof(Value));
    memcpy(callobj->dslots + nargs, slots, nvars * sizeof(Value));
}

void
js_PutCallObject(JSContext *cx, JSStackFrame *fp)
{
    JSObject *callobj = fp->getCallObj();

    /* Get the arguments object to snapshot fp's actual argument values. */
    if (fp->hasArgsObj()) {
        if (!(fp->flags & JSFRAME_OVERRIDE_ARGS))
            callobj->setSlot(JSSLOT_CALL_ARGUMENTS, ObjectOrNullValue(fp->getArgsObj()));
        js_PutArgsObject(cx, fp);
    }

    JSFunction *fun = fp->getFunction();
    JS_ASSERT(fun == js_GetCallObjectFunction(callobj));
    uintN n = fun->countArgsAndVars();

    /*
     * Since for a call object all fixed slots happen to be taken, we can copy
     * arguments and variables straight into JSObject.dslots.
     */
    JS_STATIC_ASSERT(JS_INITIAL_NSLOTS - JSSLOT_PRIVATE ==
                     1 + CALL_CLASS_FIXED_RESERVED_SLOTS);
    if (n != 0) {
        JS_ASSERT(callobj->numSlots() >= JS_INITIAL_NSLOTS + n);
        n += JS_INITIAL_NSLOTS;
        CopyValuesToCallObject(callobj, fun->nargs, fp->argv, fun->u.i.nvars, fp->slots());
    }

    /* Clear private pointers to fp, which is about to go away (js_Invoke). */
    if (js_IsNamedLambda(fun)) {
        JSObject *env = callobj->getParent();

        JS_ASSERT(env->getClass() == &js_DeclEnvClass);
        JS_ASSERT(env->getPrivate() == fp);
        env->setPrivate(NULL);
    }

    callobj->setPrivate(NULL);
    fp->setCallObj(NULL);
}

JSBool JS_FASTCALL
js_PutCallObjectOnTrace(JSContext *cx, JSObject *scopeChain, uint32 nargs, Value *argv,
                        uint32 nvars, Value *slots)
{
    JS_ASSERT(scopeChain->hasClass(&js_CallClass));
    JS_ASSERT(!scopeChain->getPrivate());

    uintN n = nargs + nvars;
    if (n != 0)
        CopyValuesToCallObject(scopeChain, nargs, argv, nvars, slots);

    return true;
}

JS_DEFINE_CALLINFO_6(extern, BOOL, js_PutCallObjectOnTrace, CONTEXT, OBJECT, UINT32, VALUEPTR,
                     UINT32, VALUEPTR, 0, nanojit::ACCSET_STORE_ANY)

static JSBool
call_enumerate(JSContext *cx, JSObject *obj)
{
    JSFunction *fun;
    uintN n, i;
    void *mark;
    jsuword *names;
    JSBool ok;
    JSAtom *name;
    JSObject *pobj;
    JSProperty *prop;

    fun = js_GetCallObjectFunction(obj);
    n = fun ? fun->countArgsAndVars() : 0;
    if (n == 0)
        return JS_TRUE;

    mark = JS_ARENA_MARK(&cx->tempPool);

    MUST_FLOW_THROUGH("out");
    names = js_GetLocalNameArray(cx, fun, &cx->tempPool);
    if (!names) {
        ok = JS_FALSE;
        goto out;
    }

    for (i = 0; i != n; ++i) {
        name = JS_LOCAL_NAME_TO_ATOM(names[i]);
        if (!name)
            continue;

        /*
         * Trigger reflection by looking up the name of the argument or
         * variable.
         */
        ok = js_LookupProperty(cx, obj, ATOM_TO_JSID(name), &pobj, &prop);
        if (!ok)
            goto out;

        /*
         * The call object will always have a property corresponding to the
         * argument or variable name because call_resolve creates the property
         * using JSPROP_PERMANENT.
         */
        JS_ASSERT(prop);
        JS_ASSERT(pobj == obj);
        pobj->dropProperty(cx, prop);
    }
    ok = JS_TRUE;

  out:
    JS_ARENA_RELEASE(&cx->tempPool, mark);
    return ok;
}

enum JSCallPropertyKind {
    JSCPK_ARGUMENTS,
    JSCPK_ARG,
    JSCPK_VAR,
    JSCPK_UPVAR
};

static JSBool
CallPropertyOp(JSContext *cx, JSObject *obj, jsid id, Value *vp,
               JSCallPropertyKind kind, JSBool setter = false)
{
    JS_ASSERT(obj->getClass() == &js_CallClass);

    uintN i = 0;
    if (kind != JSCPK_ARGUMENTS) {
        JS_ASSERT((int16) JSID_TO_INT(id) == JSID_TO_INT(id));
        i = (uint16) JSID_TO_INT(id);
    }

    Value *array;
    if (kind == JSCPK_UPVAR) {
        JSObject *callee = &obj->getSlot(JSSLOT_CALLEE).toObject();

#ifdef DEBUG
        JSFunction *callee_fun = (JSFunction *) callee->getPrivate();
        JS_ASSERT(FUN_FLAT_CLOSURE(callee_fun));
        JS_ASSERT(i < callee_fun->u.i.nupvars);
#endif

        array = callee->dslots;
    } else {
        JSFunction *fun = js_GetCallObjectFunction(obj);
        JS_ASSERT_IF(kind == JSCPK_ARG, i < fun->nargs);
        JS_ASSERT_IF(kind == JSCPK_VAR, i < fun->u.i.nvars);

        JSStackFrame *fp = (JSStackFrame *) obj->getPrivate();

        if (kind == JSCPK_ARGUMENTS) {
            if (setter) {
                if (fp)
                    fp->flags |= JSFRAME_OVERRIDE_ARGS;
                obj->setSlot(JSSLOT_CALL_ARGUMENTS, *vp);
            } else {
                if (fp && !(fp->flags & JSFRAME_OVERRIDE_ARGS)) {
                    JSObject *argsobj;

                    argsobj = js_GetArgsObject(cx, fp);
                    if (!argsobj)
                        return false;
                    vp->setObject(*argsobj);
                } else {
                    *vp = obj->getSlot(JSSLOT_CALL_ARGUMENTS);
                }
            }
            return true;
        }

        if (!fp) {
            i += CALL_CLASS_FIXED_RESERVED_SLOTS;
            if (kind == JSCPK_VAR)
                i += fun->nargs;
            else
                JS_ASSERT(kind == JSCPK_ARG);
            return setter
                   ? JS_SetReservedSlot(cx, obj, i, Jsvalify(*vp))
                   : JS_GetReservedSlot(cx, obj, i, Jsvalify(vp));
        }

        if (kind == JSCPK_ARG) {
            array = fp->argv;
        } else {
            JS_ASSERT(kind == JSCPK_VAR);
            array = fp->slots();
        }
    }

    if (setter) {
        GC_POKE(cx, array[i]);
        array[i] = *vp;
    } else {
        *vp = array[i];
    }
    return true;
}

static JSBool
GetCallArguments(JSContext *cx, JSObject *obj, jsid id, Value *vp)
{
    return CallPropertyOp(cx, obj, id, vp, JSCPK_ARGUMENTS);
}

static JSBool
SetCallArguments(JSContext *cx, JSObject *obj, jsid id, Value *vp)
{
    return CallPropertyOp(cx, obj, id, vp, JSCPK_ARGUMENTS, true);
}

JSBool
js_GetCallArg(JSContext *cx, JSObject *obj, jsid id, Value *vp)
{
    return CallPropertyOp(cx, obj, id, vp, JSCPK_ARG);
}

JSBool
SetCallArg(JSContext *cx, JSObject *obj, jsid id, Value *vp)
{
    return CallPropertyOp(cx, obj, id, vp, JSCPK_ARG, true);
}

JSBool
GetFlatUpvar(JSContext *cx, JSObject *obj, jsid id, Value *vp)
{
    return CallPropertyOp(cx, obj, id, vp, JSCPK_UPVAR);
}

JSBool
SetFlatUpvar(JSContext *cx, JSObject *obj, jsid id, Value *vp)
{
    return CallPropertyOp(cx, obj, id, vp, JSCPK_UPVAR, true);
}

JSBool
js_GetCallVar(JSContext *cx, JSObject *obj, jsid id, Value *vp)
{
    return CallPropertyOp(cx, obj, id, vp, JSCPK_VAR);
}

JSBool
js_GetCallVarChecked(JSContext *cx, JSObject *obj, jsid id, Value *vp)
{
    if (!CallPropertyOp(cx, obj, id, vp, JSCPK_VAR))
        return false;

    return CheckForEscapingClosure(cx, obj, vp);
}

JSBool
SetCallVar(JSContext *cx, JSObject *obj, jsid id, Value *vp)
{
    return CallPropertyOp(cx, obj, id, vp, JSCPK_VAR, true);
}

#if JS_TRACER
JSBool JS_FASTCALL
js_SetCallArg(JSContext *cx, JSObject *obj, jsid slotid, ValueArgType arg)
{
    Value argcopy = ValueArgToConstRef(arg);
    return CallPropertyOp(cx, obj, slotid, &argcopy, JSCPK_ARG, true);
}
JS_DEFINE_CALLINFO_4(extern, BOOL, js_SetCallArg, CONTEXT, OBJECT, JSID, VALUE, 0,
                     nanojit::ACCSET_STORE_ANY)

JSBool JS_FASTCALL
js_SetCallVar(JSContext *cx, JSObject *obj, jsid slotid, ValueArgType arg)
{
    Value argcopy = ValueArgToConstRef(arg);
    return CallPropertyOp(cx, obj, slotid, &argcopy, JSCPK_VAR, true);
}
JS_DEFINE_CALLINFO_4(extern, BOOL, js_SetCallVar, CONTEXT, OBJECT, JSID, VALUE, 0,
                     nanojit::ACCSET_STORE_ANY)
#endif

static JSBool
call_resolve(JSContext *cx, JSObject *obj, jsid id, uintN flags,
             JSObject **objp)
{
    JSFunction *fun;
    JSLocalKind localKind;
    PropertyOp getter, setter;
    uintN slot, attrs;

    JS_ASSERT(obj->getClass() == &js_CallClass);
    JS_ASSERT(!obj->getProto());

    if (!JSID_IS_ATOM(id))
        return JS_TRUE;

    const Value &callee = obj->getSlot(JSSLOT_CALLEE);
    if (callee.isUndefined())
        return JS_TRUE;
    fun = GET_FUNCTION_PRIVATE(cx, &callee.toObject());

    /*
     * Check whether the id refers to a formal parameter, local variable or
     * the arguments special name.
     *
     * We define all such names using JSDNP_DONT_PURGE to avoid an expensive
     * shape invalidation in js_DefineNativeProperty. If such an id happens to
     * shadow a global or upvar of the same name, any inner functions can
     * never access the outer binding. Thus it cannot invalidate any property
     * cache entries or derived trace guards for the outer binding. See also
     * comments in js_PurgeScopeChainHelper from jsobj.cpp.
     */
    localKind = js_LookupLocal(cx, fun, JSID_TO_ATOM(id), &slot);
    if (localKind != JSLOCAL_NONE) {
        JS_ASSERT((uint16) slot == slot);

        /*
         * We follow 10.2.3 of ECMA 262 v3 and make argument and variable
         * properties of the Call objects enumerable.
         */
        attrs = JSPROP_ENUMERATE | JSPROP_PERMANENT | JSPROP_SHARED;
        if (localKind == JSLOCAL_ARG) {
            JS_ASSERT(slot < fun->nargs);
            getter = js_GetCallArg;
            setter = SetCallArg;
        } else {
            JSCallPropertyKind cpkind;
            if (localKind == JSLOCAL_UPVAR) {
                if (!FUN_FLAT_CLOSURE(fun))
                    return JS_TRUE;
                getter = GetFlatUpvar;
                setter = SetFlatUpvar;
                cpkind = JSCPK_UPVAR;
            } else {
                JS_ASSERT(localKind == JSLOCAL_VAR || localKind == JSLOCAL_CONST);
                JS_ASSERT(slot < fun->u.i.nvars);
                getter = js_GetCallVar;
                setter = SetCallVar;
                cpkind = JSCPK_VAR;
                if (localKind == JSLOCAL_CONST)
                    attrs |= JSPROP_READONLY;
            }

            /*
             * Use js_GetCallVarChecked if the local's value is a null closure.
             * This way we penalize performance only slightly on first use of a
             * null closure, not on every use.
             */
            Value v;
            if (!CallPropertyOp(cx, obj, INT_TO_JSID((int16)slot), &v, cpkind))
                return JS_FALSE;
            JSObject *funobj;
            if (IsFunctionObject(v, &funobj) &&
                GET_FUNCTION_PRIVATE(cx, funobj)->needsWrapper()) {
                getter = js_GetCallVarChecked;
            }
        }
        if (!js_DefineNativeProperty(cx, obj, id, UndefinedValue(), getter, setter,
                                     attrs, JSScopeProperty::HAS_SHORTID, (int16) slot,
                                     NULL, JSDNP_DONT_PURGE)) {
            return JS_FALSE;
        }
        *objp = obj;
        return JS_TRUE;
    }

    /*
     * Resolve arguments so that we never store a particular Call object's
     * arguments object reference in a Call prototype's |arguments| slot.
     */
    if (JSID_IS_ATOM(id, cx->runtime->atomState.argumentsAtom)) {
        if (!js_DefineNativeProperty(cx, obj, id, UndefinedValue(),
                                     GetCallArguments, SetCallArguments,
                                     JSPROP_PERMANENT | JSPROP_SHARED,
                                     0, 0, NULL, JSDNP_DONT_PURGE)) {
            return JS_FALSE;
        }
        *objp = obj;
        return JS_TRUE;
    }

    /* Control flow reaches here only if id was not resolved. */
    return JS_TRUE;
}

JS_PUBLIC_DATA(Class) js_CallClass = {
    "Call",
    JSCLASS_HAS_PRIVATE |
    JSCLASS_HAS_RESERVED_SLOTS(CALL_CLASS_FIXED_RESERVED_SLOTS) |
    JSCLASS_NEW_RESOLVE | JSCLASS_IS_ANONYMOUS | JSCLASS_MARK_IS_TRACE,
    PropertyStub,   /* addProperty */
    PropertyStub,   /* delProperty */
    PropertyStub,   /* getProperty */
    PropertyStub,   /* setProperty */
    call_enumerate,
    (JSResolveOp)call_resolve,
    NULL,           /* convert */
    NULL,           /* finalize */
    NULL,           /* reserved0   */
    NULL,           /* checkAccess */
    NULL,           /* call        */
    NULL,           /* construct   */
    NULL,           /* xdrObject   */
    NULL,           /* hasInstance */
    JS_CLASS_TRACE(args_or_call_trace)
};

bool
JSStackFrame::getValidCalleeObject(JSContext *cx, Value *vp)
{
    if (!hasFunction()) {
        *vp = argv ? argv[-2] : UndefinedValue();
        return true;
    }

    JSFunction *fun = getFunction();

    /*
     * See the equivalent condition in args_getProperty for ARGS_CALLEE, but
     * note that here we do not want to throw, since this escape can happen via
     * a foo.caller reference alone, without any debugger or indirect eval. And
     * alas, it seems foo.caller is still used on the Web.
     */
    if (fun->needsWrapper()) {
        JSObject *wrapper = WrapEscapingClosure(cx, this, fun);
        if (!wrapper)
            return false;
        vp->setObject(*wrapper);
        return true;
    }

    JSObject *funobj = &calleeObject();
    vp->setObject(*funobj);

    /*
     * Check for an escape attempt by a joined function object, which must go
     * through the frame's |this| object's method read barrier for the method
     * atom by which it was uniquely associated with a property.
     */
    if (getThisValue().isObject()) {
        JS_ASSERT(GET_FUNCTION_PRIVATE(cx, funobj) == fun);

        if (fun == funobj && fun->methodAtom()) {
            JSObject *thisp = &getThisValue().toObject();
            JS_ASSERT(thisp->canHaveMethodBarrier());

            JSScope *scope = thisp->scope();
            if (scope->hasMethodBarrier()) {
                JSScopeProperty *sprop = scope->lookup(ATOM_TO_JSID(fun->methodAtom()));

                /*
                 * The method property might have been deleted while the method
                 * barrier scope flag stuck, so we must lookup and test here.
                 *
                 * Two cases follow: the method barrier was not crossed yet, so
                 * we cross it here; the method barrier *was* crossed, in which
                 * case we must fetch and validate the cloned (unjoined) funobj
                 * in the method property's slot.
                 *
                 * In either case we must allow for the method property to have
                 * been replaced, or its value to have been overwritten.
                 */
                if (sprop) {
                    if (sprop->isMethod() && &sprop->methodObject() == funobj) {
                        if (!scope->methodReadBarrier(cx, sprop, vp))
                            return false;
                        setCalleeObject(vp->toObject());
                        return true;
                    }
                    if (sprop->hasSlot()) {
                        Value v = thisp->getSlot(sprop->slot);
                        JSObject *clone;

                        if (IsFunctionObject(v, &clone) &&
                            GET_FUNCTION_PRIVATE(cx, clone) == fun &&
                            clone->hasMethodObj(*thisp)) {
                            JS_ASSERT(clone != funobj);
                            *vp = v;
                            setCalleeObject(*clone);
                            return true;
                        }
                    }
                }

                /*
                 * If control flows here, we can't find an already-existing
                 * clone (or force to exist a fresh clone) created via thisp's
                 * method read barrier, so we must clone fun and store it in
                 * fp's callee to avoid re-cloning upon repeated foo.caller
                 * access. It seems that there are no longer any properties
                 * referring to fun.
                 */
                funobj = CloneFunctionObject(cx, fun, fun->getParent());
                if (!funobj)
                    return false;
                funobj->setMethodObj(*thisp);
                setCalleeObject(*funobj);
                return true;
            }
        }
    }

    return true;
}

/* Generic function tinyids. */
enum {
    FUN_ARGUMENTS   = -1,       /* predefined arguments local variable */
    FUN_LENGTH      = -2,       /* number of actual args, arity if inactive */
    FUN_ARITY       = -3,       /* number of formal parameters; desired argc */
    FUN_NAME        = -4,       /* function name, "" if anonymous */
    FUN_CALLER      = -5        /* Function.prototype.caller, backward compat */
};

static JSBool
fun_getProperty(JSContext *cx, JSObject *obj, jsid id, Value *vp)
{
    if (!JSID_IS_INT(id))
        return true;

    jsint slot = JSID_TO_INT(id);

    /*
     * Loop because getter and setter can be delegated from another class,
     * but loop only for FUN_LENGTH because we must pretend that f.length
     * is in each function instance f, per ECMA-262, instead of only in the
     * Function.prototype object (we use JSPROP_PERMANENT with JSPROP_SHARED
     * to make it appear so).
     *
     * This code couples tightly to the attributes for lazyFunctionDataProps[]
     * and poisonPillProps[] initializers below, and to js_SetProperty and
     * js_HasOwnProperty.
     *
     * It's important to allow delegating objects, even though they inherit
     * this getter (fun_getProperty), to override arguments, arity, caller,
     * and name.  If we didn't return early for slot != FUN_LENGTH, we would
     * clobber *vp with the native property value, instead of letting script
     * override that value in delegating objects.
     *
     * Note how that clobbering is what simulates JSPROP_READONLY for all of
     * the non-standard properties when the directly addressed object (obj)
     * is a function object (i.e., when this loop does not iterate).
     */
    JSFunction *fun;
    while (!(fun = (JSFunction *)
                   GetInstancePrivate(cx, obj, &js_FunctionClass, NULL))) {
        if (slot != FUN_LENGTH)
            return true;
        obj = obj->getProto();
        if (!obj)
            return true;
    }

    /* Find fun's top-most activation record. */
    JSStackFrame *fp;
    for (fp = js_GetTopStackFrame(cx);
         fp && (fp->maybeFunction() != fun || (fp->flags & JSFRAME_SPECIAL));
         fp = fp->down) {
        continue;
    }

    switch (slot) {
      case FUN_ARGUMENTS:
        /* Warn if strict about f.arguments or equivalent unqualified uses. */
        if (!JS_ReportErrorFlagsAndNumber(cx,
                                          JSREPORT_WARNING | JSREPORT_STRICT,
                                          js_GetErrorMessage, NULL,
                                          JSMSG_DEPRECATED_USAGE,
                                          js_arguments_str)) {
            return false;
        }
        if (fp) {
            if (!js_GetArgsValue(cx, fp, vp))
                return false;
        } else {
            vp->setNull();
        }
        break;

      case FUN_LENGTH:
      case FUN_ARITY:
        vp->setInt32(fun->nargs);
        break;

      case FUN_NAME:
        vp->setString(fun->atom ? ATOM_TO_STRING(fun->atom)
                                : cx->runtime->emptyString);
        break;

      case FUN_CALLER:
        vp->setNull();
        if (fp && fp->down && !fp->down->getValidCalleeObject(cx, vp))
            return false;

        if (vp->isObject()) {
            /* Censor the caller if it is from another compartment. */
            if (vp->toObject().getCompartment(cx) != cx->compartment)
                vp->setNull();
        }
        break;

      default:
        /* XXX fun[0] and fun.arguments[0] are equivalent. */
        if (fp && fp->hasFunction() && (uintN)slot < fp->getFunction()->nargs)
            *vp = fp->argv[slot];
        break;
    }

    return true;
}

namespace {

struct LazyFunctionDataProp {
    uint16      atomOffset;
    int8        tinyid;
    uint8       attrs;
};

struct PoisonPillProp {
    uint16       atomOffset;
    int8         tinyid;
};

/* NB: no sentinels at ends -- use JS_ARRAY_LENGTH to bound loops. */

const LazyFunctionDataProp lazyFunctionDataProps[] = {
    {ATOM_OFFSET(arity),     FUN_ARITY,      JSPROP_PERMANENT},
    {ATOM_OFFSET(name),      FUN_NAME,       JSPROP_PERMANENT},
};

/* Properties censored into [[ThrowTypeError]] in strict mode. */
const PoisonPillProp poisonPillProps[] = {
    {ATOM_OFFSET(arguments), FUN_ARGUMENTS },
    {ATOM_OFFSET(caller),    FUN_CALLER    },
};

}

static JSBool
fun_enumerate(JSContext *cx, JSObject *obj)
{
    JS_ASSERT(obj->isFunction());

    jsval v;
    jsid id;

    if (!obj->getFunctionPrivate()->isBound()) {
        id = ATOM_TO_JSID(cx->runtime->atomState.classPrototypeAtom);
        if (!JS_LookupPropertyById(cx, obj, id, &v))
            return false;
    }

    id = ATOM_TO_JSID(cx->runtime->atomState.lengthAtom);
    if (!JS_LookupPropertyById(cx, obj, id, &v))
        return false;

    for (uintN i = 0; i < JS_ARRAY_LENGTH(lazyFunctionDataProps); i++) {
        const LazyFunctionDataProp &lfp = lazyFunctionDataProps[i];
        id = ATOM_TO_JSID(OFFSET_TO_ATOM(cx->runtime, lfp.atomOffset));
        if (!JS_LookupPropertyById(cx, obj, id, &v))
            return false;
    }

    for (uintN i = 0; i < JS_ARRAY_LENGTH(poisonPillProps); i++) {
        const PoisonPillProp &p = poisonPillProps[i];
        id = ATOM_TO_JSID(OFFSET_TO_ATOM(cx->runtime, p.atomOffset));
        if (!JS_LookupPropertyById(cx, obj, id, &v))
            return false;
    }

    return true;
}

static JSBool
fun_resolve(JSContext *cx, JSObject *obj, jsid id, uintN flags,
            JSObject **objp)
{
    if (!JSID_IS_ATOM(id))
        return JS_TRUE;

    JSFunction *fun = obj->getFunctionPrivate();

    /*
     * No need to reflect fun.prototype in 'fun.prototype = ... '. Assert that
     * fun is not a compiler-created function object, which must never leak to
     * script or embedding code and then be mutated.
     */
    if ((flags & JSRESOLVE_ASSIGNING) && !JSID_IS_ATOM(id, cx->runtime->atomState.lengthAtom)) {
        JS_ASSERT(!IsInternalFunctionObject(obj));
        return JS_TRUE;
    }

    /*
     * Ok, check whether id is 'prototype' and bootstrap the function object's
     * prototype property.
     */
    JSAtom *atom = cx->runtime->atomState.classPrototypeAtom;
    if (id == ATOM_TO_JSID(atom)) {
        JS_ASSERT(!IsInternalFunctionObject(obj));

        /*
         * Beware of the wacky case of a user function named Object -- trying
         * to find a prototype for that will recur back here _ad perniciem_.
         */
        if (fun->atom == CLASS_ATOM(cx, Object))
            return JS_TRUE;

        /* ES5 15.3.4.5: bound functions don't have a prototype property. */
        if (fun->isBound())
            return JS_TRUE;

        /*
         * Make the prototype object an instance of Object with the same parent
         * as the function object itself.
         */
        JSObject *parent = obj->getParent();
        JSObject *proto;
        if (!js_GetClassPrototype(cx, parent, JSProto_Object, &proto))
            return JS_FALSE;
        proto = NewNativeClassInstance(cx, &js_ObjectClass, proto, parent);
        if (!proto)
            return JS_FALSE;

        /*
         * ECMA (15.3.5.2) says that constructor.prototype is DontDelete for
         * user-defined functions, but DontEnum | ReadOnly | DontDelete for
         * native "system" constructors such as Object or Function.  So lazily
         * set the former here in fun_resolve, but eagerly define the latter
         * in JS_InitClass, with the right attributes.
         */
        if (!js_SetClassPrototype(cx, obj, proto, JSPROP_PERMANENT))
            return JS_FALSE;

        *objp = obj;
        return JS_TRUE;
    }

    atom = cx->runtime->atomState.lengthAtom;
    if (id == ATOM_TO_JSID(atom)) {
        JS_ASSERT(!IsInternalFunctionObject(obj));
        if (!js_DefineNativeProperty(cx, obj, ATOM_TO_JSID(atom), Int32Value(fun->nargs),
                                     PropertyStub, PropertyStub,
                                     JSPROP_PERMANENT | JSPROP_READONLY, 0, 0, NULL)) {
            return JS_FALSE;
        }
        *objp = obj;
        return JS_TRUE;
    }

    for (uintN i = 0; i < JS_ARRAY_LENGTH(lazyFunctionDataProps); i++) {
        const LazyFunctionDataProp *lfp = &lazyFunctionDataProps[i];

        atom = OFFSET_TO_ATOM(cx->runtime, lfp->atomOffset);
        if (id == ATOM_TO_JSID(atom)) {
            JS_ASSERT(!IsInternalFunctionObject(obj));

            if (!js_DefineNativeProperty(cx, obj,
                                         ATOM_TO_JSID(atom), UndefinedValue(),
                                         fun_getProperty, PropertyStub,
                                         lfp->attrs, JSScopeProperty::HAS_SHORTID,
                                         lfp->tinyid, NULL)) {
                return JS_FALSE;
            }
            *objp = obj;
            return JS_TRUE;
        }
    }

    for (uintN i = 0; i < JS_ARRAY_LENGTH(poisonPillProps); i++) {
        const PoisonPillProp &p = poisonPillProps[i];

        atom = OFFSET_TO_ATOM(cx->runtime, p.atomOffset);
        if (id == ATOM_TO_JSID(atom)) {
            JS_ASSERT(!IsInternalFunctionObject(obj));

            PropertyOp getter, setter;
            uintN attrs = JSPROP_PERMANENT;
            if (fun->inStrictMode() || fun->isBound()) {
                JSObject *throwTypeError = obj->getThrowTypeError();

                getter = CastAsPropertyOp(throwTypeError);
                setter = CastAsPropertyOp(throwTypeError);
                attrs |= JSPROP_GETTER | JSPROP_SETTER;
            } else {
                getter = fun_getProperty;
                setter = PropertyStub;
            }

            if (!js_DefineNativeProperty(cx, obj, ATOM_TO_JSID(atom), UndefinedValue(),
                                         getter, setter,
                                         attrs, JSScopeProperty::HAS_SHORTID,
                                         p.tinyid, NULL)) {
                return JS_FALSE;
            }
            *objp = obj;
            return JS_TRUE;
        }
    }

    return JS_TRUE;
}

#if JS_HAS_XDR

/* XXX store parent and proto, if defined */
JSBool
js_XDRFunctionObject(JSXDRState *xdr, JSObject **objp)
{
    JSContext *cx;
    JSFunction *fun;
    uint32 firstword;           /* flag telling whether fun->atom is non-null,
                                   plus for fun->u.i.skipmin, fun->u.i.wrapper,
                                   and 14 bits reserved for future use */
    uintN nargs, nvars, nupvars, n;
    uint32 localsword;          /* word for argument and variable counts */
    uint32 flagsword;           /* word for fun->u.i.nupvars and fun->flags */

    cx = xdr->cx;
    if (xdr->mode == JSXDR_ENCODE) {
        fun = GET_FUNCTION_PRIVATE(cx, *objp);
        if (!FUN_INTERPRETED(fun)) {
            JS_ReportErrorNumber(cx, js_GetErrorMessage, NULL,
                                 JSMSG_NOT_SCRIPTED_FUNCTION,
                                 JS_GetFunctionName(fun));
            return false;
        }
        if (fun->u.i.wrapper) {
            JS_ReportErrorNumber(cx, js_GetErrorMessage, NULL,
                                 JSMSG_XDR_CLOSURE_WRAPPER,
                                 JS_GetFunctionName(fun));
            return false;
        }
        JS_ASSERT((fun->u.i.wrapper & ~1U) == 0);
        firstword = (fun->u.i.skipmin << 2) | (fun->u.i.wrapper << 1) | !!fun->atom;
        nargs = fun->nargs;
        nvars = fun->u.i.nvars;
        nupvars = fun->u.i.nupvars;
        localsword = (nargs << 16) | nvars;
        flagsword = (nupvars << 16) | fun->flags;
    } else {
        fun = js_NewFunction(cx, NULL, NULL, 0, JSFUN_INTERPRETED, NULL, NULL);
        if (!fun)
            return false;
        FUN_OBJECT(fun)->clearParent();
        FUN_OBJECT(fun)->clearProto();
#ifdef __GNUC__
        nvars = nargs = nupvars = 0;    /* quell GCC uninitialized warning */
#endif
    }

    AutoObjectRooter tvr(cx, FUN_OBJECT(fun));

    if (!JS_XDRUint32(xdr, &firstword))
        return false;
    if ((firstword & 1U) && !js_XDRAtom(xdr, &fun->atom))
        return false;
    if (!JS_XDRUint32(xdr, &localsword) ||
        !JS_XDRUint32(xdr, &flagsword)) {
        return false;
    }

    if (xdr->mode == JSXDR_DECODE) {
        nargs = localsword >> 16;
        nvars = uint16(localsword);
        JS_ASSERT((flagsword & JSFUN_KINDMASK) >= JSFUN_INTERPRETED);
        nupvars = flagsword >> 16;
        fun->flags = uint16(flagsword);
        fun->u.i.skipmin = uint16(firstword >> 2);
        fun->u.i.wrapper = JSPackedBool((firstword >> 1) & 1);
    }

    /* do arguments and local vars */
    n = nargs + nvars + nupvars;
    if (n != 0) {
        void *mark;
        uintN i;
        uintN bitmapLength;
        uint32 *bitmap;
        jsuword *names;
        JSAtom *name;
        JSLocalKind localKind;

        bool ok = true;
        mark = JS_ARENA_MARK(&xdr->cx->tempPool);

        /*
         * From this point the control must flow via the label release_mark.
         *
         * To xdr the names we prefix the names with a bitmap descriptor and
         * then xdr the names as strings. For argument names (indexes below
         * nargs) the corresponding bit in the bitmap is unset when the name
         * is null. Such null names are not encoded or decoded. For variable
         * names (indexes starting from nargs) bitmap's bit is set when the
         * name is declared as const, not as ordinary var.
         * */
        MUST_FLOW_THROUGH("release_mark");
        bitmapLength = JS_HOWMANY(n, JS_BITS_PER_UINT32);
        JS_ARENA_ALLOCATE_CAST(bitmap, uint32 *, &xdr->cx->tempPool,
                               bitmapLength * sizeof *bitmap);
        if (!bitmap) {
            js_ReportOutOfScriptQuota(xdr->cx);
            ok = false;
            goto release_mark;
        }
        if (xdr->mode == JSXDR_ENCODE) {
            names = js_GetLocalNameArray(xdr->cx, fun, &xdr->cx->tempPool);
            if (!names) {
                ok = false;
                goto release_mark;
            }
            PodZero(bitmap, bitmapLength);
            for (i = 0; i != n; ++i) {
                if (i < fun->nargs
                    ? JS_LOCAL_NAME_TO_ATOM(names[i]) != NULL
                    : JS_LOCAL_NAME_IS_CONST(names[i])) {
                    bitmap[i >> JS_BITS_PER_UINT32_LOG2] |=
                        JS_BIT(i & (JS_BITS_PER_UINT32 - 1));
                }
            }
        }
#ifdef __GNUC__
        else {
            names = NULL;   /* quell GCC uninitialized warning */
        }
#endif
        for (i = 0; i != bitmapLength; ++i) {
            ok = !!JS_XDRUint32(xdr, &bitmap[i]);
            if (!ok)
                goto release_mark;
        }
        for (i = 0; i != n; ++i) {
            if (i < nargs &&
                !(bitmap[i >> JS_BITS_PER_UINT32_LOG2] &
                  JS_BIT(i & (JS_BITS_PER_UINT32 - 1)))) {
                if (xdr->mode == JSXDR_DECODE) {
                    ok = !!js_AddLocal(xdr->cx, fun, NULL, JSLOCAL_ARG);
                    if (!ok)
                        goto release_mark;
                } else {
                    JS_ASSERT(!JS_LOCAL_NAME_TO_ATOM(names[i]));
                }
                continue;
            }
            if (xdr->mode == JSXDR_ENCODE)
                name = JS_LOCAL_NAME_TO_ATOM(names[i]);
            ok = !!js_XDRAtom(xdr, &name);
            if (!ok)
                goto release_mark;
            if (xdr->mode == JSXDR_DECODE) {
                localKind = (i < nargs)
                            ? JSLOCAL_ARG
                            : (i < nargs + nvars)
                            ? (bitmap[i >> JS_BITS_PER_UINT32_LOG2] &
                               JS_BIT(i & (JS_BITS_PER_UINT32 - 1))
                               ? JSLOCAL_CONST
                               : JSLOCAL_VAR)
                            : JSLOCAL_UPVAR;
                ok = !!js_AddLocal(xdr->cx, fun, name, localKind);
                if (!ok)
                    goto release_mark;
            }
        }

      release_mark:
        JS_ARENA_RELEASE(&xdr->cx->tempPool, mark);
        if (!ok)
            return false;

        if (xdr->mode == JSXDR_DECODE)
            js_FreezeLocalNames(cx, fun);
    }

    if (!js_XDRScript(xdr, &fun->u.i.script, false, NULL))
        return false;

    if (xdr->mode == JSXDR_DECODE) {
        *objp = FUN_OBJECT(fun);
        if (fun->u.i.script != JSScript::emptyScript()) {
#ifdef CHECK_SCRIPT_OWNER
            fun->u.i.script->owner = NULL;
#endif
            js_CallNewScriptHook(cx, fun->u.i.script, fun);
        }
    }

    return true;
}

#else  /* !JS_HAS_XDR */

#define js_XDRFunctionObject NULL

#endif /* !JS_HAS_XDR */

/*
 * [[HasInstance]] internal method for Function objects: fetch the .prototype
 * property of its 'this' parameter, and walks the prototype chain of v (only
 * if v is an object) returning true if .prototype is found.
 */
static JSBool
fun_hasInstance(JSContext *cx, JSObject *obj, const Value *v, JSBool *bp)
{
    while (obj->isFunction()) {
        if (!obj->getFunctionPrivate()->isBound())
            break;
        obj = obj->getBoundFunctionTarget();
    }

    jsid id = ATOM_TO_JSID(cx->runtime->atomState.classPrototypeAtom);
    Value pval;
    if (!obj->getProperty(cx, id, &pval))
        return JS_FALSE;

    if (pval.isPrimitive()) {
        /*
         * Throw a runtime error if instanceof is called on a function that
         * has a non-object as its .prototype value.
         */
        js_ReportValueError(cx, JSMSG_BAD_PROTOTYPE, -1, ObjectValue(*obj), NULL);
        return JS_FALSE;
    }

    *bp = js_IsDelegate(cx, &pval.toObject(), *v);
    return JS_TRUE;
}

static void
TraceLocalNames(JSTracer *trc, JSFunction *fun);

static void
DestroyLocalNames(JSContext *cx, JSFunction *fun);

static void
fun_trace(JSTracer *trc, JSObject *obj)
{
    /* A newborn function object may have a not yet initialized private slot. */
    JSFunction *fun = (JSFunction *) obj->getPrivate();
    if (!fun)
        return;

    if (FUN_OBJECT(fun) != obj) {
        /* obj is cloned function object, trace the original. */
        JS_CALL_TRACER(trc, FUN_OBJECT(fun), JSTRACE_OBJECT, "private");
        return;
    }
    if (fun->atom)
        JS_CALL_STRING_TRACER(trc, ATOM_TO_STRING(fun->atom), "atom");
    if (FUN_INTERPRETED(fun)) {
        if (fun->u.i.script)
            js_TraceScript(trc, fun->u.i.script);
        TraceLocalNames(trc, fun);
    }
}

static void
fun_finalize(JSContext *cx, JSObject *obj)
{
    /* Ignore newborn and cloned function objects. */
    JSFunction *fun = (JSFunction *) obj->getPrivate();
    if (!fun || FUN_OBJECT(fun) != obj)
        return;

    /*
     * Null-check of u.i.script is required since the parser sets interpreted
     * very early.
     */
    if (FUN_INTERPRETED(fun)) {
        if (fun->u.i.script)
            js_DestroyScript(cx, fun->u.i.script);
        DestroyLocalNames(cx, fun);
    }
}

int
JSFunction::sharpSlotBase(JSContext *cx)
{
#if JS_HAS_SHARP_VARS
    JSAtom *name = js_Atomize(cx, "#array", 6, 0);
    if (name) {
        uintN index = uintN(-1);
#ifdef DEBUG
        JSLocalKind kind =
#endif
            js_LookupLocal(cx, this, name, &index);
        JS_ASSERT(kind == JSLOCAL_VAR);
        return int(index);
    }
#endif
    return -1;
}

uint32
JSFunction::countInterpretedReservedSlots() const
{
    JS_ASSERT(FUN_INTERPRETED(this));

    return (u.i.nupvars == 0) ? 0 : u.i.script->upvars()->length;
}

/*
 * Reserve two slots in all function objects for XPConnect.  Note that this
 * does not bloat every instance, only those on which reserved slots are set,
 * and those on which ad-hoc properties are defined.
 */
JS_PUBLIC_DATA(Class) js_FunctionClass = {
    js_Function_str,
    JSCLASS_HAS_PRIVATE | JSCLASS_NEW_RESOLVE |
    JSCLASS_HAS_RESERVED_SLOTS(JSObject::FUN_FIXED_RESERVED_SLOTS) |
    JSCLASS_MARK_IS_TRACE | JSCLASS_HAS_CACHED_PROTO(JSProto_Function),
    PropertyStub,   /* addProperty */
    PropertyStub,   /* delProperty */
    PropertyStub,   /* getProperty */
    PropertyStub,   /* setProperty */
    fun_enumerate,
    (JSResolveOp)fun_resolve,
    ConvertStub,
    fun_finalize,
    NULL,           /* reserved0   */
    NULL,           /* checkAccess */
    NULL,           /* call        */
    NULL,           /* construct   */
    js_XDRFunctionObject,
    fun_hasInstance,
    JS_CLASS_TRACE(fun_trace)
};

namespace js {

JSString *
fun_toStringHelper(JSContext *cx, JSObject *obj, uintN indent)
{
    if (!obj->isFunction()) {
        if (obj->isFunctionProxy())
            return JSProxy::fun_toString(cx, obj, indent);
        JS_ReportErrorNumber(cx, js_GetErrorMessage, NULL,
                             JSMSG_INCOMPATIBLE_PROTO,
                             js_Function_str, js_toString_str,
                             "object");
        return NULL;
    }

    JSFunction *fun = GET_FUNCTION_PRIVATE(cx, obj);
    if (!fun)
        return NULL;
    return JS_DecompileFunction(cx, fun, indent);
}

}  /* namespace js */

static JSBool
fun_toString(JSContext *cx, uintN argc, Value *vp)
{
    JS_ASSERT(IsFunctionObject(vp[0]));
    uint32_t indent = 0;

    if (argc != 0 && !ValueToECMAUint32(cx, vp[2], &indent))
        return false;

    JSObject *obj = ComputeThisFromVp(cx, vp);
    if (!obj)
        return false;

    JSString *str = fun_toStringHelper(cx, obj, indent);
    if (!str)
        return false;

    vp->setString(str);
    return true;
}

#if JS_HAS_TOSOURCE
static JSBool
fun_toSource(JSContext *cx, uintN argc, Value *vp)
{
    JS_ASSERT(IsFunctionObject(vp[0]));

    JSObject *obj = ComputeThisFromVp(cx, vp);
    if (!obj)
        return false;

    JSString *str = fun_toStringHelper(cx, obj, JS_DONT_PRETTY_PRINT);
    if (!str)
        return false;

    vp->setString(str);
    return true;
}
#endif

JSBool
js_fun_call(JSContext *cx, uintN argc, Value *vp)
{
    LeaveTrace(cx);

    JSObject *obj = ComputeThisFromVp(cx, vp);
    if (!obj)
        return JS_FALSE;
    Value fval = vp[1];

    if (!js_IsCallable(fval)) {
        JSString *str = js_ValueToString(cx, fval);
        if (str) {
            const char *bytes = js_GetStringBytes(cx, str);

            if (bytes) {
                JS_ReportErrorNumber(cx, js_GetErrorMessage, NULL,
                                     JSMSG_INCOMPATIBLE_PROTO,
                                     js_Function_str, js_call_str,
                                     bytes);
            }
        }
        return JS_FALSE;
    }

    Value *argv = vp + 2;
    if (argc == 0) {
        /* Call fun with its global object as the 'this' param if no args. */
        obj = NULL;
    } else {
        /* Otherwise convert the first arg to 'this' and skip over it. */
        if (argv[0].isObject())
            obj = &argv[0].toObject();
        else if (!js_ValueToObjectOrNull(cx, argv[0], &obj))
            return JS_FALSE;
        argc--;
        argv++;
    }

    /* Allocate stack space for fval, obj, and the args. */
    InvokeArgsGuard args;
    if (!cx->stack().pushInvokeArgs(cx, argc, args))
        return JS_FALSE;

    /* Push fval, obj, and the args. */
    args.callee() = fval;
    args.thisv().setObjectOrNull(obj);
    memcpy(args.argv(), argv, argc * sizeof *argv);

    bool ok = Invoke(cx, args, 0);
    *vp = args.rval();
    return ok;
}

/* ES5 15.3.4.3 */
JSBool
js_fun_apply(JSContext *cx, uintN argc, Value *vp)
{
    JSObject *obj = ComputeThisFromVp(cx, vp);
    if (!obj)
        return false;

    /* Step 1. */
    Value fval = vp[1];
    if (!js_IsCallable(fval)) {
        if (JSString *str = js_ValueToString(cx, fval)) {
            if (const char *bytes = js_GetStringBytes(cx, str)) {
                JS_ReportErrorNumber(cx, js_GetErrorMessage, NULL,
                                     JSMSG_INCOMPATIBLE_PROTO,
                                     js_Function_str, js_apply_str,
                                     bytes);
            }
        }
        return false;
    }

    /* Step 2. */
    if (argc < 2 || vp[3].isNullOrUndefined())
        return js_fun_call(cx, (argc > 0) ? 1 : 0, vp);

    /* Step 3. */
    if (!vp[3].isObject()) {
        JS_ReportErrorNumber(cx, js_GetErrorMessage, NULL, JSMSG_BAD_APPLY_ARGS, js_apply_str);
        return false;
    }

    /*
     * Steps 4-5 (note erratum removing steps originally numbered 5 and 7 in
     * original version of ES5).
     */
    JSObject *aobj = vp[3].toObject().wrappedObject(cx);
    jsuint length;
    if (aobj->isArray()) {
        length = aobj->getArrayLength();
    } else if (aobj->isArguments() && !aobj->isArgsLengthOverridden()) {
        length = aobj->getArgsInitialLength();
    } else {
        Value &lenval = vp[0];
        if (!aobj->getProperty(cx, ATOM_TO_JSID(cx->runtime->atomState.lengthAtom), &lenval))
            return false;

        if (lenval.isInt32()) {
            length = jsuint(lenval.toInt32()); /* jsuint cast does ToUint32 */
        } else {
            JS_STATIC_ASSERT(sizeof(jsuint) == sizeof(uint32_t));
            if (!ValueToECMAUint32(cx, lenval, (uint32_t *)&length))
                return false;
        }
    }

    /* Convert the first arg to 'this' and skip over it. */
    if (vp[2].isObject())
        obj = &vp[2].toObject();
    else if (!js_ValueToObjectOrNull(cx, vp[2], &obj))
        return JS_FALSE;

    LeaveTrace(cx);

    /* Step 6. */
    uintN n = uintN(JS_MIN(length, JS_ARGS_LENGTH_MAX));

    InvokeArgsGuard args;
    if (!cx->stack().pushInvokeArgs(cx, n, args))
        return false;

    /* Push fval, obj, and aobj's elements as args. */
    args.callee() = fval;
    args.thisv().setObjectOrNull(obj);

    /* Steps 7-8. */
    if (aobj && aobj->isArguments() && !aobj->isArgsLengthOverridden()) {
        /*
         * Two cases, two loops: note how in the case of an active stack frame
         * backing aobj, even though we copy from fp->argv, we still must check
         * aobj->getArgsElement(i) for a hole, to handle a delete on the
         * corresponding arguments element. See args_delProperty.
         */
        JSStackFrame *fp = (JSStackFrame *) aobj->getPrivate();
        Value *argv = args.argv();
        if (fp) {
            memcpy(argv, fp->argv, n * sizeof(Value));
            for (uintN i = 0; i < n; i++) {
                if (aobj->getArgsElement(i).isMagic(JS_ARGS_HOLE)) // suppress deleted element
                    argv[i].setUndefined();
            }
        } else {
            for (uintN i = 0; i < n; i++) {
                argv[i] = aobj->getArgsElement(i);
                if (argv[i].isMagic(JS_ARGS_HOLE))
                    argv[i].setUndefined();
            }
        }
    } else {
        Value *argv = args.argv();
        for (uintN i = 0; i < n; i++) {
            if (!aobj->getProperty(cx, INT_TO_JSID(jsint(i)), &argv[i]))
                return JS_FALSE;
        }
    }

    /* Step 9. */
    if (!Invoke(cx, args, 0))
        return false;
    *vp = args.rval();
    return true;
}

namespace {
Native
FastNativeToNative(FastNative fn)
{
    return reinterpret_cast<Native>(fn);
}

JSBool
CallOrConstructBoundFunction(JSContext *cx, uintN argc, Value *vp);
}

inline bool
JSFunction::isBound() const
{
    return isFastNative() && u.n.native == FastNativeToNative(CallOrConstructBoundFunction);
}

inline bool
JSObject::initBoundFunction(JSContext *cx, const Value &thisArg,
                            const Value *args, uintN argslen)
{
    JS_ASSERT(isFunction());
    JS_ASSERT(getFunctionPrivate()->isBound());

    fslots[JSSLOT_BOUND_FUNCTION_THIS] = thisArg;
    fslots[JSSLOT_BOUND_FUNCTION_ARGS_COUNT].setPrivateUint32(argslen);
    if (argslen != 0) {
        if (!js_EnsureReservedSlots(cx, this, argslen))
            return false;

        JS_ASSERT(dslots);
        JS_ASSERT(dslots[-1].toPrivateUint32() >= argslen);
        memcpy(&dslots[0], args, argslen * sizeof(Value));
    }
    return true;
}

inline JSObject *
JSObject::getBoundFunctionTarget() const
{
    JS_ASSERT(isFunction());
    JS_ASSERT(getFunctionPrivate()->isBound());

    /* Bound functions abuse |parent| to store their target function. */
    return getParent();
}

inline const js::Value &
JSObject::getBoundFunctionThis() const
{
    JS_ASSERT(isFunction());
    JS_ASSERT(getFunctionPrivate()->isBound());

    return fslots[JSSLOT_BOUND_FUNCTION_THIS];
}

inline const js::Value *
JSObject::getBoundFunctionArguments(uintN &argslen) const
{
    JS_ASSERT(isFunction());
    JS_ASSERT(getFunctionPrivate()->isBound());

    argslen = fslots[JSSLOT_BOUND_FUNCTION_ARGS_COUNT].toPrivateUint32();
    JS_ASSERT_IF(argslen > 0, dslots);
    JS_ASSERT_IF(argslen > 0, dslots[-1].toPrivateUint32() >= argslen);
    return &dslots[0];
}

namespace {

/* ES5 15.3.4.5.1 and 15.3.4.5.2. */
JSBool
CallOrConstructBoundFunction(JSContext *cx, uintN argc, Value *vp)
{
    JSObject *obj = &vp[0].toObject();
    JS_ASSERT(obj->isFunction());
    JS_ASSERT(obj->getFunctionPrivate()->isBound());

    LeaveTrace(cx);

    bool constructing = vp[1].isMagic(JS_FAST_CONSTRUCTOR);

    /* 15.3.4.5.1 step 1, 15.3.4.5.2 step 3. */
    uintN argslen;
    const Value *boundArgs = obj->getBoundFunctionArguments(argslen);

    if (argc + argslen > JS_ARGS_LENGTH_MAX) {
        js_ReportAllocationOverflow(cx);
        return false;
    }

    /* 15.3.4.5.1 step 3, 15.3.4.5.2 step 1. */
    JSObject *target = obj->getBoundFunctionTarget();

    /* 15.3.4.5.1 step 2. */
    const Value &boundThis = obj->getBoundFunctionThis();

    InvokeArgsGuard args;
    if (!cx->stack().pushInvokeArgs(cx, argc + argslen, args))
        return false;

    /* 15.3.4.5.1, 15.3.4.5.2 step 4. */
    memcpy(args.argv(), boundArgs, argslen * sizeof(Value));
    memcpy(args.argv() + argslen, vp + 2, argc * sizeof(Value));

    /* 15.3.4.5.1, 15.3.4.5.2 step 5. */
    args.callee().setObject(*target);

    if (!constructing) {
        /*
         * FIXME Pass boundThis directly without boxing!  This will go away
         *       very shortly when this-boxing only occurs for non-strict
         *       functions, callee-side, in bug 514570.
         */
        JSObject *boundThisObj;
        if (boundThis.isObjectOrNull()) {
            boundThisObj = boundThis.toObjectOrNull();
        } else {
            if (!js_ValueToObjectOrNull(cx, boundThis, &boundThisObj))
                return false;
        }

        args.thisv() = ObjectOrNullValue(boundThisObj);
    }

    if (constructing ? !InvokeConstructor(cx, args) : !Invoke(cx, args, 0))
        return false;

    *vp = args.rval();
    return true;
}

/* ES5 15.3.4.5. */
JSBool
fun_bind(JSContext *cx, uintN argc, Value *vp)
{
    /* Step 1. */
    JSObject *target = ComputeThisFromVp(cx, vp);
    if (!target)
        return false;

    /* Step 2. */
    if (!target->wrappedObject(cx)->isCallable()) {
        if (JSString *str = js_ValueToString(cx, vp[1])) {
            if (const char *bytes = js_GetStringBytes(cx, str)) {
                JS_ReportErrorNumber(cx, js_GetErrorMessage, NULL,
                                     JSMSG_INCOMPATIBLE_PROTO,
                                     js_Function_str, "bind", bytes);
            }
        }
        return false;
    }

    /* Step 3. */
    Value *args = NULL;
    uintN argslen = 0;
    if (argc > 1) {
        args = vp + 3;
        argslen = argc - 1;
    }

    /* Steps 15-16. */
    uintN length = 0;
    if (target->isFunction()) {
        uintN nargs = target->getFunctionPrivate()->nargs;
        if (nargs > argslen)
            length = nargs - argslen;
    }

    /* Step 4-6, 10-11. */
    JSAtom *name = target->isFunction() ? target->getFunctionPrivate()->atom : NULL;

    /* NB: Bound functions abuse |parent| to store their target. */
    JSObject *funobj =
        js_NewFunction(cx, NULL, FastNativeToNative(CallOrConstructBoundFunction), length,
                       JSFUN_FAST_NATIVE | JSFUN_FAST_NATIVE_CTOR, target, name);
    if (!funobj)
        return false;

    /* Steps 7-9. */
    Value thisArg = argc >= 1 ? vp[2] : UndefinedValue();
    if (!funobj->initBoundFunction(cx, thisArg, args, argslen))
        return false;

    /* Steps 17, 19-21 are handled by fun_resolve. */
    /* Step 18 is the default for new functions. */

    /* Step 22. */
    vp->setObject(*funobj);
    return true;
}

}

static JSFunctionSpec function_methods[] = {
#if JS_HAS_TOSOURCE
    JS_FN(js_toSource_str,   fun_toSource,   0,0),
#endif
    JS_FN(js_toString_str,   fun_toString,   0,0),
    JS_FN(js_apply_str,      js_fun_apply,   2,0),
    JS_FN(js_call_str,       js_fun_call,    1,0),
    JS_FN("bind",            fun_bind,       1,0),
    JS_FS_END
};

static JSBool
Function(JSContext *cx, JSObject *obj, uintN argc, Value *argv, Value *rval)
{
    JSFunction *fun;
    JSObject *parent;
    JSStackFrame *fp, *caller;
    uintN i, n, lineno;
    JSAtom *atom;
    const char *filename;
    JSBool ok;
    JSString *str, *arg;
    TokenStream ts(cx);
    JSPrincipals *principals;
    jschar *collected_args, *cp;
    void *mark;
    size_t arg_length, args_length, old_args_length;
    TokenKind tt;

    if (!JS_IsConstructing(cx)) {
        obj = NewFunction(cx, NULL);
        if (!obj)
            return JS_FALSE;
        rval->setObject(*obj);
    } else {
        /*
         * The constructor is called before the private slot is initialized so
         * we must use getPrivate, not GET_FUNCTION_PRIVATE here.
         */
        if (obj->getPrivate())
            return JS_TRUE;
    }

    /*
     * NB: (new Function) is not lexically closed by its caller, it's just an
     * anonymous function in the top-level scope that its constructor inhabits.
     * Thus 'var x = 42; f = new Function("return x"); print(f())' prints 42,
     * and so would a call to f from another top-level's script or function.
     *
     * In older versions, before call objects, a new Function was adopted by
     * its running context's globalObject, which might be different from the
     * top-level reachable from scopeChain (in HTML frames, e.g.).
     */
    parent = argv[-2].toObject().getParent();

    fun = js_NewFunction(cx, obj, NULL, 0, JSFUN_LAMBDA | JSFUN_INTERPRETED,
                         parent, cx->runtime->atomState.anonymousAtom);

    if (!fun)
        return JS_FALSE;

    /*
     * Function is static and not called directly by other functions in this
     * file, therefore it is callable only as a native function by js_Invoke.
     * Find the scripted caller, possibly skipping other native frames such as
     * are built for Function.prototype.call or .apply activations that invoke
     * Function indirectly from a script.
     */
    fp = js_GetTopStackFrame(cx);
    JS_ASSERT(!fp->hasScript() && fp->hasFunction() &&
              fp->getFunction()->u.n.native == Function);
    caller = js_GetScriptedCaller(cx, fp);
    if (caller) {
        principals = JS_EvalFramePrincipals(cx, fp, caller);
        filename = js_ComputeFilename(cx, caller, principals, &lineno);
    } else {
        filename = NULL;
        lineno = 0;
        principals = NULL;
    }

    /* Belt-and-braces: check that the caller has access to parent. */
    if (!js_CheckPrincipalsAccess(cx, parent, principals,
                                  CLASS_ATOM(cx, Function))) {
        return JS_FALSE;
    }

    /*
     * CSP check: whether new Function() is allowed at all.
     * Report errors via CSP is done in the script security manager.
     * js_CheckContentSecurityPolicy is defined in jsobj.cpp
     */
    if (!js_CheckContentSecurityPolicy(cx)) {
        JS_ReportErrorNumber(cx, js_GetErrorMessage, NULL, JSMSG_CSP_BLOCKED_FUNCTION);
        return JS_FALSE;
    }

    n = argc ? argc - 1 : 0;
    if (n > 0) {
        enum { OK, BAD, BAD_FORMAL } state;

        /*
         * Collect the function-argument arguments into one string, separated
         * by commas, then make a tokenstream from that string, and scan it to
         * get the arguments.  We need to throw the full scanner at the
         * problem, because the argument string can legitimately contain
         * comments and linefeeds.  XXX It might be better to concatenate
         * everything up into a function definition and pass it to the
         * compiler, but doing it this way is less of a delta from the old
         * code.  See ECMA 15.3.2.1.
         */
        state = BAD_FORMAL;
        args_length = 0;
        for (i = 0; i < n; i++) {
            /* Collect the lengths for all the function-argument arguments. */
            arg = js_ValueToString(cx, argv[i]);
            if (!arg)
                return JS_FALSE;
            argv[i].setString(arg);

            /*
             * Check for overflow.  The < test works because the maximum
             * JSString length fits in 2 fewer bits than size_t has.
             */
            old_args_length = args_length;
            args_length = old_args_length + arg->length();
            if (args_length < old_args_length) {
                js_ReportAllocationOverflow(cx);
                return JS_FALSE;
            }
        }

        /* Add 1 for each joining comma and check for overflow (two ways). */
        old_args_length = args_length;
        args_length = old_args_length + n - 1;
        if (args_length < old_args_length ||
            args_length >= ~(size_t)0 / sizeof(jschar)) {
            js_ReportAllocationOverflow(cx);
            return JS_FALSE;
        }

        /*
         * Allocate a string to hold the concatenated arguments, including room
         * for a terminating 0.  Mark cx->tempPool for later release, to free
         * collected_args and its tokenstream in one swoop.
         */
        mark = JS_ARENA_MARK(&cx->tempPool);
        JS_ARENA_ALLOCATE_CAST(cp, jschar *, &cx->tempPool,
                               (args_length+1) * sizeof(jschar));
        if (!cp) {
            js_ReportOutOfScriptQuota(cx);
            return JS_FALSE;
        }
        collected_args = cp;

        /*
         * Concatenate the arguments into the new string, separated by commas.
         */
        for (i = 0; i < n; i++) {
            arg = argv[i].toString();
            arg_length = arg->length();
            (void) js_strncpy(cp, arg->chars(), arg_length);
            cp += arg_length;

            /* Add separating comma or terminating 0. */
            *cp++ = (i + 1 < n) ? ',' : 0;
        }

        /* Initialize a tokenstream that reads from the given string. */
        if (!ts.init(collected_args, args_length, NULL, filename, lineno)) {
            JS_ARENA_RELEASE(&cx->tempPool, mark);
            return JS_FALSE;
        }

        /* The argument string may be empty or contain no tokens. */
        tt = ts.getToken();
        if (tt != TOK_EOF) {
            for (;;) {
                /*
                 * Check that it's a name.  This also implicitly guards against
                 * TOK_ERROR, which was already reported.
                 */
                if (tt != TOK_NAME)
                    goto after_args;

                /*
                 * Get the atom corresponding to the name from the token
                 * stream; we're assured at this point that it's a valid
                 * identifier.
                 */
                atom = ts.currentToken().t_atom;

                /* Check for a duplicate parameter name. */
                if (js_LookupLocal(cx, fun, atom, NULL) != JSLOCAL_NONE) {
                    const char *name;

                    name = js_AtomToPrintableString(cx, atom);
                    ok = name && ReportCompileErrorNumber(cx, &ts, NULL,
                                                          JSREPORT_WARNING | JSREPORT_STRICT,
                                                          JSMSG_DUPLICATE_FORMAL, name);
                    if (!ok)
                        goto after_args;
                }
                if (!js_AddLocal(cx, fun, atom, JSLOCAL_ARG))
                    goto after_args;

                /*
                 * Get the next token.  Stop on end of stream.  Otherwise
                 * insist on a comma, get another name, and iterate.
                 */
                tt = ts.getToken();
                if (tt == TOK_EOF)
                    break;
                if (tt != TOK_COMMA)
                    goto after_args;
                tt = ts.getToken();
            }
        }

        state = OK;
      after_args:
        if (state == BAD_FORMAL && !ts.isError()) {
            /*
             * Report "malformed formal parameter" iff no illegal char or
             * similar scanner error was already reported.
             */
            JS_ReportErrorNumber(cx, js_GetErrorMessage, NULL,
                                 JSMSG_BAD_FORMAL);
        }
        ts.close();
        JS_ARENA_RELEASE(&cx->tempPool, mark);
        if (state != OK)
            return JS_FALSE;
    }

    if (argc) {
        str = js_ValueToString(cx, argv[argc-1]);
        if (!str)
            return JS_FALSE;
        argv[argc-1].setString(str);
    } else {
        str = cx->runtime->emptyString;
    }

    return Compiler::compileFunctionBody(cx, fun, principals,
                                         str->chars(), str->length(),
                                         filename, lineno);
}

namespace {

JSBool
ThrowTypeError(JSContext *cx, uintN argc, Value *vp)
{
    JS_ReportErrorFlagsAndNumber(cx, JSREPORT_ERROR, js_GetErrorMessage, NULL,
                                 JSMSG_THROW_TYPE_ERROR);
    return false;
}

}

JSObject *
js_InitFunctionClass(JSContext *cx, JSObject *obj)
{
    JSObject *proto = js_InitClass(cx, obj, NULL, &js_FunctionClass, Function, 1,
                                   NULL, function_methods, NULL, NULL);
    if (!proto)
        return NULL;

    JSFunction *fun = js_NewFunction(cx, proto, NULL, 0, JSFUN_INTERPRETED, obj, NULL);
    if (!fun)
        return NULL;
    fun->u.i.script = JSScript::emptyScript();

    if (obj->getClass()->flags & JSCLASS_IS_GLOBAL) {
        /* ES5 13.2.3: Construct the unique [[ThrowTypeError]] function object. */
        JSObject *throwTypeError =
            js_NewFunction(cx, NULL, reinterpret_cast<Native>(ThrowTypeError), 0,
                           JSFUN_FAST_NATIVE, obj, NULL);
        if (!throwTypeError)
            return NULL;

        JS_ALWAYS_TRUE(js_SetReservedSlot(cx, obj, JSRESERVED_GLOBAL_THROWTYPEERROR,
                                          ObjectValue(*throwTypeError)));
    }

    return proto;
}

JSFunction *
js_NewFunction(JSContext *cx, JSObject *funobj, Native native, uintN nargs,
               uintN flags, JSObject *parent, JSAtom *atom)
{
    JSFunction *fun;

    if (funobj) {
        JS_ASSERT(funobj->isFunction());
        funobj->setParent(parent);
    } else {
        funobj = NewFunction(cx, parent);
        if (!funobj)
            return NULL;
    }
    JS_ASSERT(!funobj->getPrivate());
    fun = (JSFunction *) funobj;

    /* Initialize all function members. */
    fun->nargs = uint16(nargs);
    fun->flags = flags & (JSFUN_FLAGS_MASK | JSFUN_KINDMASK |
                          JSFUN_TRCINFO | JSFUN_FAST_NATIVE_CTOR);
    if ((flags & JSFUN_KINDMASK) >= JSFUN_INTERPRETED) {
        JS_ASSERT(!native);
        JS_ASSERT(nargs == 0);
        fun->u.i.nvars = 0;
        fun->u.i.nupvars = 0;
        fun->u.i.skipmin = 0;
        fun->u.i.wrapper = false;
        fun->u.i.script = NULL;
#ifdef DEBUG
        fun->u.i.names.taggedAtom = 0;
#endif
    } else {
        fun->u.n.extra = 0;
        fun->u.n.spare = 0;
        fun->u.n.clasp = NULL;
        if (flags & JSFUN_TRCINFO) {
#ifdef JS_TRACER
            JSNativeTraceInfo *trcinfo =
                JS_FUNC_TO_DATA_PTR(JSNativeTraceInfo *, native);
            fun->u.n.native = (js::Native) trcinfo->native;
            fun->u.n.trcinfo = trcinfo;
#else
            fun->u.n.trcinfo = NULL;
#endif
        } else {
            fun->u.n.native = native;
            fun->u.n.trcinfo = NULL;
        }
        JS_ASSERT(fun->u.n.native);
    }
    fun->atom = atom;

    /* Set private to self to indicate non-cloned fully initialized function. */
    FUN_OBJECT(fun)->setPrivate(fun);
    return fun;
}

JSObject * JS_FASTCALL
js_CloneFunctionObject(JSContext *cx, JSFunction *fun, JSObject *parent,
                       JSObject *proto)
{
    JS_ASSERT(parent);
    JS_ASSERT(proto);

    /*
     * The cloned function object does not need the extra JSFunction members
     * beyond JSObject as it points to fun via the private slot.
     */
    JSObject *clone = NewNativeClassInstance(cx, &js_FunctionClass, proto, parent);
    if (!clone)
        return NULL;
    clone->setPrivate(fun);
    return clone;
}

#ifdef JS_TRACER
JS_DEFINE_CALLINFO_4(extern, OBJECT, js_CloneFunctionObject, CONTEXT, FUNCTION, OBJECT, OBJECT, 0,
                     nanojit::ACCSET_STORE_ANY)
#endif

/*
 * Create a new flat closure, but don't initialize the imported upvar
 * values. The tracer calls this function and then initializes the upvar
 * slots on trace.
 */
JSObject * JS_FASTCALL
js_AllocFlatClosure(JSContext *cx, JSFunction *fun, JSObject *scopeChain)
{
    JS_ASSERT(FUN_FLAT_CLOSURE(fun));
    JS_ASSERT((fun->u.i.script->upvarsOffset
               ? fun->u.i.script->upvars()->length
               : 0) == fun->u.i.nupvars);

    JSObject *closure = CloneFunctionObject(cx, fun, scopeChain);
    if (!closure)
        return closure;

    uint32 nslots = fun->countInterpretedReservedSlots();
    if (nslots == 0)
        return closure;
    if (!js_EnsureReservedSlots(cx, closure, nslots))
        return NULL;

    return closure;
}

JS_DEFINE_CALLINFO_3(extern, OBJECT, js_AllocFlatClosure,
                     CONTEXT, FUNCTION, OBJECT, 0, nanojit::ACCSET_STORE_ANY)

JS_REQUIRES_STACK JSObject *
js_NewFlatClosure(JSContext *cx, JSFunction *fun)
{
    /*
     * Flat closures can be partial, they may need to search enclosing scope
     * objects via JSOP_NAME, etc.
     */
    JSObject *scopeChain = js_GetScopeChain(cx, cx->fp());
    if (!scopeChain)
        return NULL;

    JSObject *closure = js_AllocFlatClosure(cx, fun, scopeChain);
    if (!closure || fun->u.i.nupvars == 0)
        return closure;

    JSUpvarArray *uva = fun->u.i.script->upvars();
    JS_ASSERT(uva->length <= closure->dslots[-1].toPrivateUint32());

    uintN level = fun->u.i.script->staticLevel;
    for (uint32 i = 0, n = uva->length; i < n; i++)
        closure->dslots[i] = GetUpvar(cx, level, uva->vector[i]);

    return closure;
}

JSObject *
js_NewDebuggableFlatClosure(JSContext *cx, JSFunction *fun)
{
    JS_ASSERT(cx->fp()->getFunction()->flags & JSFUN_HEAVYWEIGHT);
    JS_ASSERT(!cx->fp()->getFunction()->optimizedClosure());
    JS_ASSERT(FUN_FLAT_CLOSURE(fun));

    return WrapEscapingClosure(cx, cx->fp(), fun);
}

JSFunction *
js_DefineFunction(JSContext *cx, JSObject *obj, JSAtom *atom, Native native,
                  uintN nargs, uintN attrs)
{
    PropertyOp gsop;
    JSFunction *fun;

    if (attrs & JSFUN_STUB_GSOPS) {
        /*
         * JSFUN_STUB_GSOPS is a request flag only, not stored in fun->flags or
         * the defined property's attributes. This allows us to encode another,
         * internal flag using the same bit, JSFUN_EXPR_CLOSURE -- see jsfun.h
         * for more on this.
         */
        attrs &= ~JSFUN_STUB_GSOPS;
        gsop = PropertyStub;
    } else {
        gsop = NULL;
    }
    fun = js_NewFunction(cx, NULL, native, nargs,
                         attrs & (JSFUN_FLAGS_MASK | JSFUN_TRCINFO), obj, atom);
    if (!fun)
        return NULL;
    if (!obj->defineProperty(cx, ATOM_TO_JSID(atom), ObjectValue(*fun),
                             gsop, gsop, attrs & ~JSFUN_FLAGS_MASK)) {
        return NULL;
    }
    return fun;
}

#if (JSV2F_CONSTRUCT & JSV2F_SEARCH_STACK)
# error "JSINVOKE_CONSTRUCT and JSV2F_SEARCH_STACK are not disjoint!"
#endif

JSFunction *
js_ValueToFunction(JSContext *cx, const Value *vp, uintN flags)
{
    JSObject *funobj;
    if (!IsFunctionObject(*vp, &funobj)) {
        js_ReportIsNotFunction(cx, vp, flags);
        return NULL;
    }
    return GET_FUNCTION_PRIVATE(cx, funobj);
}

JSObject *
js_ValueToFunctionObject(JSContext *cx, Value *vp, uintN flags)
{
    JSObject *funobj;
    if (!IsFunctionObject(*vp, &funobj)) {
        js_ReportIsNotFunction(cx, vp, flags);
        return NULL;
    }

    return funobj;
}

JSObject *
js_ValueToCallableObject(JSContext *cx, Value *vp, uintN flags)
{
    if (vp->isObject()) {
        JSObject *callable = &vp->toObject();
        if (callable->isCallable())
            return callable;
    }

    js_ReportIsNotFunction(cx, vp, flags);
    return NULL;
}

void
js_ReportIsNotFunction(JSContext *cx, const Value *vp, uintN flags)
{
    const char *name = NULL, *source = NULL;
    AutoValueRooter tvr(cx);
    uintN error = (flags & JSV2F_CONSTRUCT) ? JSMSG_NOT_CONSTRUCTOR : JSMSG_NOT_FUNCTION;
    LeaveTrace(cx);

    /*
     * We try to the print the code that produced vp if vp is a value in the
     * most recent interpreted stack frame. Note that additional values, not
     * directly produced by the script, may have been pushed onto the frame's
     * expression stack (e.g. by InvokeFromEngine) thereby incrementing sp past
     * the depth simulated by ReconstructPCStack. Since we must pass an offset
     * from the top of the simulated stack to js_ReportValueError3, it is
     * important to do bounds checking using the simulated, rather than actual,
     * stack depth.
     */
    ptrdiff_t spindex = 0;

    FrameRegsIter i(cx);
    while (!i.done() && !i.pc())
        ++i;

    if (!i.done()) {
        uintN depth = js_ReconstructStackDepth(cx, i.fp()->getScript(), i.pc());
        Value *simsp = i.fp()->base() + depth;
        JS_ASSERT(simsp <= i.sp());
        if (i.fp()->base() <= vp && vp < simsp)
            spindex = vp - simsp;
    }

    if (!spindex)
        spindex = ((flags & JSV2F_SEARCH_STACK) ? JSDVG_SEARCH_STACK : JSDVG_IGNORE_STACK);

    js_ReportValueError3(cx, error, spindex, *vp, NULL, name, source);
}

/*
 * When a function has between 2 and MAX_ARRAY_LOCALS arguments and variables,
 * their name are stored as the JSLocalNames.array.
 */
#define MAX_ARRAY_LOCALS 8

JS_STATIC_ASSERT(2 <= MAX_ARRAY_LOCALS);
JS_STATIC_ASSERT(MAX_ARRAY_LOCALS < JS_BITMASK(16));

/*
 * When we use a hash table to store the local names, we use a singly linked
 * list to record the indexes of duplicated parameter names to preserve the
 * duplicates for the decompiler.
 */
typedef struct JSNameIndexPair JSNameIndexPair;

struct JSNameIndexPair {
    JSAtom          *name;
    uint16          index;
    JSNameIndexPair *link;
};

struct JSLocalNameMap {
    JSDHashTable    names;
    JSNameIndexPair *lastdup;
};

typedef struct JSLocalNameHashEntry {
    JSDHashEntryHdr hdr;
    JSAtom          *name;
    uint16          index;
    uint8           localKind;
} JSLocalNameHashEntry;

static void
FreeLocalNameHash(JSContext *cx, JSLocalNameMap *map)
{
    JSNameIndexPair *dup, *next;

    for (dup = map->lastdup; dup; dup = next) {
        next = dup->link;
        cx->free(dup);
    }
    JS_DHashTableFinish(&map->names);
    cx->free(map);
}

static JSBool
HashLocalName(JSContext *cx, JSLocalNameMap *map, JSAtom *name,
              JSLocalKind localKind, uintN index)
{
    JSLocalNameHashEntry *entry;
    JSNameIndexPair *dup;

    JS_ASSERT(index <= JS_BITMASK(16));
#if JS_HAS_DESTRUCTURING
    if (!name) {
        /* A destructuring pattern does not need a hash entry. */
        JS_ASSERT(localKind == JSLOCAL_ARG);
        return JS_TRUE;
    }
#endif
    entry = (JSLocalNameHashEntry *)
            JS_DHashTableOperate(&map->names, name, JS_DHASH_ADD);
    if (!entry) {
        JS_ReportOutOfMemory(cx);
        return JS_FALSE;
    }
    if (entry->name) {
        JS_ASSERT(entry->name == name);
        JS_ASSERT(entry->localKind == JSLOCAL_ARG && localKind == JSLOCAL_ARG);
        dup = (JSNameIndexPair *) cx->malloc(sizeof *dup);
        if (!dup)
            return JS_FALSE;
        dup->name = entry->name;
        dup->index = entry->index;
        dup->link = map->lastdup;
        map->lastdup = dup;
    }
    entry->name = name;
    entry->index = (uint16) index;
    entry->localKind = (uint8) localKind;
    return JS_TRUE;
}

JSBool
js_AddLocal(JSContext *cx, JSFunction *fun, JSAtom *atom, JSLocalKind kind)
{
    jsuword taggedAtom;
    uint16 *indexp;
    uintN n, i;
    jsuword *array;
    JSLocalNameMap *map;

    JS_ASSERT(FUN_INTERPRETED(fun));
    JS_ASSERT(!fun->u.i.script);
    JS_ASSERT(((jsuword) atom & 1) == 0);
    taggedAtom = (jsuword) atom;
    if (kind == JSLOCAL_ARG) {
        indexp = &fun->nargs;
    } else if (kind == JSLOCAL_UPVAR) {
        indexp = &fun->u.i.nupvars;
    } else {
        indexp = &fun->u.i.nvars;
        if (kind == JSLOCAL_CONST)
            taggedAtom |= 1;
        else
            JS_ASSERT(kind == JSLOCAL_VAR);
    }
    n = fun->countLocalNames();
    if (n == 0) {
        JS_ASSERT(fun->u.i.names.taggedAtom == 0);
        fun->u.i.names.taggedAtom = taggedAtom;
    } else if (n < MAX_ARRAY_LOCALS) {
        if (n > 1) {
            array = fun->u.i.names.array;
        } else {
            array = (jsuword *) cx->malloc(MAX_ARRAY_LOCALS * sizeof *array);
            if (!array)
                return JS_FALSE;
            array[0] = fun->u.i.names.taggedAtom;
            fun->u.i.names.array = array;
        }
        if (kind == JSLOCAL_ARG) {
            /*
             * A destructuring argument pattern adds variables, not arguments,
             * so for the following arguments nvars != 0.
             */
#if JS_HAS_DESTRUCTURING
            if (fun->u.i.nvars != 0) {
                memmove(array + fun->nargs + 1, array + fun->nargs,
                        fun->u.i.nvars * sizeof *array);
            }
#else
            JS_ASSERT(fun->u.i.nvars == 0);
#endif
            array[fun->nargs] = taggedAtom;
        } else {
            array[n] = taggedAtom;
        }
    } else if (n == MAX_ARRAY_LOCALS) {
        array = fun->u.i.names.array;
        map = (JSLocalNameMap *) cx->malloc(sizeof *map);
        if (!map)
            return JS_FALSE;
        if (!JS_DHashTableInit(&map->names, JS_DHashGetStubOps(),
                               NULL, sizeof(JSLocalNameHashEntry),
                               JS_DHASH_DEFAULT_CAPACITY(MAX_ARRAY_LOCALS
                                                         * 2))) {
            JS_ReportOutOfMemory(cx);
            cx->free(map);
            return JS_FALSE;
        }

        map->lastdup = NULL;
        for (i = 0; i != MAX_ARRAY_LOCALS; ++i) {
            taggedAtom = array[i];
            uintN j = i;
            JSLocalKind k = JSLOCAL_ARG;
            if (j >= fun->nargs) {
                j -= fun->nargs;
                if (j < fun->u.i.nvars) {
                    k = (taggedAtom & 1) ? JSLOCAL_CONST : JSLOCAL_VAR;
                } else {
                    j -= fun->u.i.nvars;
                    k = JSLOCAL_UPVAR;
                }
            }
            if (!HashLocalName(cx, map, (JSAtom *) (taggedAtom & ~1), k, j)) {
                FreeLocalNameHash(cx, map);
                return JS_FALSE;
            }
        }
        if (!HashLocalName(cx, map, atom, kind, *indexp)) {
            FreeLocalNameHash(cx, map);
            return JS_FALSE;
        }

        /*
         * At this point the entry is added and we cannot fail. It is time
         * to replace fun->u.i.names with the built map.
         */
        fun->u.i.names.map = map;
        cx->free(array);
    } else {
        if (*indexp == JS_BITMASK(16)) {
            JS_ReportErrorNumber(cx, js_GetErrorMessage, NULL,
                                 (kind == JSLOCAL_ARG)
                                 ? JSMSG_TOO_MANY_FUN_ARGS
                                 : JSMSG_TOO_MANY_LOCALS);
            return JS_FALSE;
        }
        if (!HashLocalName(cx, fun->u.i.names.map, atom, kind, *indexp))
            return JS_FALSE;
    }

    /* Update the argument or variable counter. */
    ++*indexp;
    return JS_TRUE;
}

JSLocalKind
js_LookupLocal(JSContext *cx, JSFunction *fun, JSAtom *atom, uintN *indexp)
{
    uintN n, i, upvar_base;
    jsuword *array;
    JSLocalNameHashEntry *entry;

    JS_ASSERT(FUN_INTERPRETED(fun));
    n = fun->countLocalNames();
    if (n == 0)
        return JSLOCAL_NONE;
    if (n <= MAX_ARRAY_LOCALS) {
        array = (n == 1) ? &fun->u.i.names.taggedAtom : fun->u.i.names.array;

        /* Search from the tail to pick up the last duplicated name. */
        i = n;
        upvar_base = fun->countArgsAndVars();
        do {
            --i;
            if (atom == JS_LOCAL_NAME_TO_ATOM(array[i])) {
                if (i < fun->nargs) {
                    if (indexp)
                        *indexp = i;
                    return JSLOCAL_ARG;
                }
                if (i >= upvar_base) {
                    if (indexp)
                        *indexp = i - upvar_base;
                    return JSLOCAL_UPVAR;
                }
                if (indexp)
                    *indexp = i - fun->nargs;
                return JS_LOCAL_NAME_IS_CONST(array[i])
                       ? JSLOCAL_CONST
                       : JSLOCAL_VAR;
            }
        } while (i != 0);
    } else {
        entry = (JSLocalNameHashEntry *)
                JS_DHashTableOperate(&fun->u.i.names.map->names, atom,
                                     JS_DHASH_LOOKUP);
        if (JS_DHASH_ENTRY_IS_BUSY(&entry->hdr)) {
            JS_ASSERT(entry->localKind != JSLOCAL_NONE);
            if (indexp)
                *indexp = entry->index;
            return (JSLocalKind) entry->localKind;
        }
    }
    return JSLOCAL_NONE;
}

typedef struct JSLocalNameEnumeratorArgs {
    JSFunction      *fun;
    jsuword         *names;
#ifdef DEBUG
    uintN           nCopiedArgs;
    uintN           nCopiedVars;
#endif
} JSLocalNameEnumeratorArgs;

static JSDHashOperator
get_local_names_enumerator(JSDHashTable *table, JSDHashEntryHdr *hdr,
                           uint32 number, void *arg)
{
    JSLocalNameHashEntry *entry;
    JSLocalNameEnumeratorArgs *args;
    uint i;
    jsuword constFlag;

    entry = (JSLocalNameHashEntry *) hdr;
    args = (JSLocalNameEnumeratorArgs *) arg;
    JS_ASSERT(entry->name);
    if (entry->localKind == JSLOCAL_ARG) {
        JS_ASSERT(entry->index < args->fun->nargs);
        JS_ASSERT(args->nCopiedArgs++ < args->fun->nargs);
        i = entry->index;
        constFlag = 0;
    } else {
        JS_ASSERT(entry->localKind == JSLOCAL_VAR ||
                  entry->localKind == JSLOCAL_CONST ||
                  entry->localKind == JSLOCAL_UPVAR);
        JS_ASSERT(entry->index < args->fun->u.i.nvars + args->fun->u.i.nupvars);
        JS_ASSERT(args->nCopiedVars++ < unsigned(args->fun->u.i.nvars + args->fun->u.i.nupvars));
        i = args->fun->nargs;
        if (entry->localKind == JSLOCAL_UPVAR)
           i += args->fun->u.i.nvars;
        i += entry->index;
        constFlag = (entry->localKind == JSLOCAL_CONST);
    }
    args->names[i] = (jsuword) entry->name | constFlag;
    return JS_DHASH_NEXT;
}

jsuword *
js_GetLocalNameArray(JSContext *cx, JSFunction *fun, JSArenaPool *pool)
{
    uintN n;
    jsuword *names;
    JSLocalNameMap *map;
    JSLocalNameEnumeratorArgs args;
    JSNameIndexPair *dup;

    JS_ASSERT(fun->hasLocalNames());
    n = fun->countLocalNames();

    if (n <= MAX_ARRAY_LOCALS)
        return (n == 1) ? &fun->u.i.names.taggedAtom : fun->u.i.names.array;

    /*
     * No need to check for overflow of the allocation size as we are making a
     * copy of already allocated data. As such it must fit size_t.
     */
    JS_ARENA_ALLOCATE_CAST(names, jsuword *, pool, (size_t) n * sizeof *names);
    if (!names) {
        js_ReportOutOfScriptQuota(cx);
        return NULL;
    }

#if JS_HAS_DESTRUCTURING
    /* Some parameter names can be NULL due to destructuring patterns. */
    PodZero(names, fun->nargs);
#endif
    map = fun->u.i.names.map;
    args.fun = fun;
    args.names = names;
#ifdef DEBUG
    args.nCopiedArgs = 0;
    args.nCopiedVars = 0;
#endif
    JS_DHashTableEnumerate(&map->names, get_local_names_enumerator, &args);
    for (dup = map->lastdup; dup; dup = dup->link) {
        JS_ASSERT(dup->index < fun->nargs);
        JS_ASSERT(args.nCopiedArgs++ < fun->nargs);
        names[dup->index] = (jsuword) dup->name;
    }
#if !JS_HAS_DESTRUCTURING
    JS_ASSERT(args.nCopiedArgs == fun->nargs);
#endif
    JS_ASSERT(args.nCopiedVars == fun->u.i.nvars + fun->u.i.nupvars);

    return names;
}

static JSDHashOperator
trace_local_names_enumerator(JSDHashTable *table, JSDHashEntryHdr *hdr,
                             uint32 number, void *arg)
{
    JSLocalNameHashEntry *entry;
    JSTracer *trc;

    entry = (JSLocalNameHashEntry *) hdr;
    JS_ASSERT(entry->name);
    trc = (JSTracer *) arg;
    JS_SET_TRACING_INDEX(trc,
                         entry->localKind == JSLOCAL_ARG ? "arg" : "var",
                         entry->index);
    Mark(trc, ATOM_TO_STRING(entry->name), JSTRACE_STRING);
    return JS_DHASH_NEXT;
}

static void
TraceLocalNames(JSTracer *trc, JSFunction *fun)
{
    uintN n, i;
    JSAtom *atom;
    jsuword *array;

    JS_ASSERT(FUN_INTERPRETED(fun));
    n = fun->countLocalNames();
    if (n == 0)
        return;
    if (n <= MAX_ARRAY_LOCALS) {
        array = (n == 1) ? &fun->u.i.names.taggedAtom : fun->u.i.names.array;
        i = n;
        do {
            --i;
            atom = (JSAtom *) (array[i] & ~1);
            if (atom) {
                JS_SET_TRACING_INDEX(trc,
                                     i < fun->nargs ? "arg" : "var",
                                     i < fun->nargs ? i : i - fun->nargs);
                Mark(trc, ATOM_TO_STRING(atom), JSTRACE_STRING);
            }
        } while (i != 0);
    } else {
        JS_DHashTableEnumerate(&fun->u.i.names.map->names,
                               trace_local_names_enumerator, trc);

        /*
         * No need to trace the list of duplicates in map->lastdup as the
         * names there are traced when enumerating the hash table.
         */
    }
}

void
DestroyLocalNames(JSContext *cx, JSFunction *fun)
{
    uintN n;

    n = fun->countLocalNames();
    if (n <= 1)
        return;
    if (n <= MAX_ARRAY_LOCALS)
        cx->free(fun->u.i.names.array);
    else
        FreeLocalNameHash(cx, fun->u.i.names.map);
}

void
js_FreezeLocalNames(JSContext *cx, JSFunction *fun)
{
    uintN n;
    jsuword *array;

    JS_ASSERT(FUN_INTERPRETED(fun));
    JS_ASSERT(!fun->u.i.script);
    n = fun->nargs + fun->u.i.nvars + fun->u.i.nupvars;
    if (2 <= n && n < MAX_ARRAY_LOCALS) {
        /* Shrink over-allocated array ignoring realloc failures. */
        array = (jsuword *) cx->realloc(fun->u.i.names.array,
                                        n * sizeof *array);
        if (array)
            fun->u.i.names.array = array;
    }
#ifdef DEBUG
    if (n > MAX_ARRAY_LOCALS)
        JS_DHashMarkTableImmutable(&fun->u.i.names.map->names);
#endif
}

JSAtom *
JSFunction::findDuplicateFormal() const
{
    JS_ASSERT(isInterpreted());

    if (nargs <= 1)
        return NULL;

    /* Function with two to MAX_ARRAY_LOCALS parameters use an aray. */
    unsigned n = nargs + u.i.nvars + u.i.nupvars;
    if (n <= MAX_ARRAY_LOCALS) {
        jsuword *array = u.i.names.array;

        /* Quadratic, but MAX_ARRAY_LOCALS is 8, so at most 28 comparisons. */
        for (unsigned i = 0; i < nargs; i++) {
            for (unsigned j = i + 1; j < nargs; j++) {
                if (array[i] == array[j])
                    return JS_LOCAL_NAME_TO_ATOM(array[i]);
            }
        }
        return NULL;
    }

    /*
     * Functions with more than MAX_ARRAY_LOCALS parameters use a hash
     * table. Hashed local name maps have already made a list of any
     * duplicate argument names for us.
     */
    JSNameIndexPair *dup = u.i.names.map->lastdup;
    return dup ? dup->name : NULL;
}
