/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sw=4 et tw=99:
 *
 * Test script cloning.
 */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */


#include "tests.h"
#include "jsdbgapi.h"

BEGIN_TEST(test_cloneScript)
{
    JSObject *A, *B;

    CHECK(A = createGlobal());
    CHECK(B = createGlobal());

    const char *source =
        "var i = 0;\n"
        "var sum = 0;\n"
        "while (i < 10) {\n"
        "    sum += i;\n"
        "    ++i;\n"
        "}\n"
        "(sum);\n";

    JSObject *obj;

    // compile for A
    {
        JSAutoEnterCompartment a;
        if (!a.enter(cx, A))
            return false;

        JSFunction *fun;
        CHECK(fun = JS_CompileFunction(cx, A, "f", 0, NULL, source, strlen(source), __FILE__, 1));
        CHECK(obj = JS_GetFunctionObject(fun));
    }

    // clone into B
    {
        JSAutoEnterCompartment b;
        if (!b.enter(cx, B))
            return false;

        CHECK(JS_CloneFunctionObject(cx, obj, B));
    }

    return true;
}
END_TEST(test_cloneScript)

void
DestroyPrincipals(JSPrincipals *principals)
{
    delete principals;
}

struct Principals : public JSPrincipals
{
  public:
    Principals()
    {
        refcount = 0;
    }
};

class AutoDropPrincipals
{
    JSRuntime *rt;
    JSPrincipals *principals;

  public:
    AutoDropPrincipals(JSRuntime *rt, JSPrincipals *principals)
      : rt(rt), principals(principals)
    {
        JS_HoldPrincipals(principals);
    }

    ~AutoDropPrincipals()
    {
        JS_DropPrincipals(rt, principals);
    }
};

BEGIN_TEST(test_cloneScriptWithPrincipals)
{
    JS_InitDestroyPrincipalsCallback(rt, DestroyPrincipals);

    JSPrincipals *principalsA = new Principals();
    AutoDropPrincipals dropA(rt, principalsA);
    JSPrincipals *principalsB = new Principals();
    AutoDropPrincipals dropB(rt, principalsB);

    JSObject *A, *B;

    CHECK(A = createGlobal(principalsA));
    CHECK(B = createGlobal(principalsB));

    const char *argnames[] = { "arg" };
    const char *source = "return function() { return arg; }";

    JSObject *obj;

    // Compile in A
    {
        JSAutoEnterCompartment a;
        if (!a.enter(cx, A))
            return false;

        JSFunction *fun;
        CHECK(fun = JS_CompileFunctionForPrincipals(cx, A, principalsA, "f",
                                                    mozilla::ArrayLength(argnames), argnames,
                                                    source, strlen(source), __FILE__, 1));

        JSScript *script;
        CHECK(script = JS_GetFunctionScript(cx, fun));

        CHECK(JS_GetScriptPrincipals(script) == principalsA);
        CHECK(obj = JS_GetFunctionObject(fun));
    }

    // Clone into B
    {
        JSAutoEnterCompartment b;
        if (!b.enter(cx, B))
            return false;

        JSObject *cloned;
        CHECK(cloned = JS_CloneFunctionObject(cx, obj, B));

        JSFunction *fun;
        CHECK(fun = JS_ValueToFunction(cx, JS::ObjectValue(*cloned)));

        JSScript *script;
        CHECK(script = JS_GetFunctionScript(cx, fun));

        CHECK(JS_GetScriptPrincipals(script) == principalsB);

        JS::Value v;
        JS::Value args[] = { JS::Int32Value(1) };
        CHECK(JS_CallFunctionValue(cx, B, JS::ObjectValue(*cloned), 1, args, &v));
        CHECK(v.isObject());

        JSObject *funobj = &v.toObject();
        CHECK(JS_ObjectIsFunction(cx, funobj));
        CHECK(fun = JS_ValueToFunction(cx, v));
        CHECK(script = JS_GetFunctionScript(cx, fun));
        CHECK(JS_GetScriptPrincipals(script) == principalsB);
    }

    return true;
}
END_TEST(test_cloneScriptWithPrincipals)
