/* ***** BEGIN LICENSE BLOCK *****
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
 * The Original Code is Mozilla IPCShell.
 *
 * The Initial Developer of the Original Code is
 *   Ben Turner <bent.mozilla@gmail.com>.
 * Portions created by the Initial Developer are Copyright (C) 2009
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
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

#include <stdlib.h>
#include <errno.h>
#ifdef HAVE_IO_H
#include <io.h>     /* for isatty() */
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>     /* for isatty() */
#endif

#include "base/basictypes.h"

#include "jsapi.h"
#include "jscntxt.h"
#include "jsdbgapi.h"
#include "jsprf.h"

#include "XPCShellEnvironment.h"

#include "mozilla/XPCOM.h"

#include "nsIChannel.h"
#include "nsIClassInfo.h"
#include "nsIDirectoryService.h"
#include "nsIJSContextStack.h"
#include "nsIJSRuntimeService.h"
#include "nsIPrincipal.h"
#include "nsIScriptSecurityManager.h"
#include "nsIURI.h"
#include "nsIXPConnect.h"
#include "nsIXPCScriptable.h"

#include "nsJSUtils.h"
#include "nsThreadUtils.h"
#include "nsXULAppAPI.h"

#include "TestShellChild.h"
#include "TestShellParent.h"

#define EXITCODE_RUNTIME_ERROR 3
#define EXITCODE_FILE_NOT_FOUND 4

using mozilla::ipc::XPCShellEnvironment;
using mozilla::ipc::TestShellChild;
using mozilla::ipc::TestShellParent;

namespace {

static const char kDefaultRuntimeScriptFilename[] = "xpcshell.js";

class FullTrustSecMan : public nsIScriptSecurityManager
{
public:
    NS_DECL_ISUPPORTS
    NS_DECL_NSIXPCSECURITYMANAGER
    NS_DECL_NSISCRIPTSECURITYMANAGER

    FullTrustSecMan() { }
    virtual ~FullTrustSecMan() { }

    void SetSystemPrincipal(nsIPrincipal *aPrincipal) {
        mSystemPrincipal = aPrincipal;
    }

private:
    nsCOMPtr<nsIPrincipal> mSystemPrincipal;
};

class XPCShellDirProvider : public nsIDirectoryServiceProvider
{
public:
    NS_DECL_ISUPPORTS
    NS_DECL_NSIDIRECTORYSERVICEPROVIDER

    XPCShellDirProvider() { }
    ~XPCShellDirProvider() { }

    PRBool SetGREDir(const char *dir);
    void ClearGREDir() { mGREDir = nsnull; }

private:
    nsCOMPtr<nsILocalFile> mGREDir;
};

inline XPCShellEnvironment*
Environment(JSContext* cx)
{
    XPCShellEnvironment* env = 
        static_cast<XPCShellEnvironment*>(JS_GetContextPrivate(cx));
    NS_ASSERTION(env, "Should never be null!");
    return env;
}

static void
ScriptErrorReporter(JSContext *cx,
                    const char *message,
                    JSErrorReport *report)
{
    int i, j, k, n;
    char *prefix = NULL, *tmp;
    const char *ctmp;
    JSStackFrame * fp = nsnull;
    nsCOMPtr<nsIXPConnect> xpc;

    // Don't report an exception from inner JS frames as the callers may intend
    // to handle it.
    while ((fp = JS_FrameIterator(cx, &fp))) {
        if (!JS_IsNativeFrame(cx, fp)) {
            return;
        }
    }

    // In some cases cx->fp is null here so use XPConnect to tell us about inner
    // frames.
    if ((xpc = do_GetService(nsIXPConnect::GetCID()))) {
        nsAXPCNativeCallContext *cc = nsnull;
        xpc->GetCurrentNativeCallContext(&cc);
        if (cc) {
            nsAXPCNativeCallContext *prev = cc;
            while (NS_SUCCEEDED(prev->GetPreviousCallContext(&prev)) && prev) {
                PRUint16 lang;
                if (NS_SUCCEEDED(prev->GetLanguage(&lang)) &&
                    lang == nsAXPCNativeCallContext::LANG_JS) {
                    return;
                }
            }
        }
    }

    if (!report) {
        fprintf(stderr, "%s\n", message);
        return;
    }

    /* Conditionally ignore reported warnings. */
    if (JSREPORT_IS_WARNING(report->flags) &&
        !Environment(cx)->ShouldReportWarnings()) {
        return;
    }

    if (report->filename)
        prefix = JS_smprintf("%s:", report->filename);
    if (report->lineno) {
        tmp = prefix;
        prefix = JS_smprintf("%s%u: ", tmp ? tmp : "", report->lineno);
        JS_free(cx, tmp);
    }
    if (JSREPORT_IS_WARNING(report->flags)) {
        tmp = prefix;
        prefix = JS_smprintf("%s%swarning: ",
                             tmp ? tmp : "",
                             JSREPORT_IS_STRICT(report->flags) ? "strict " : "");
        JS_free(cx, tmp);
    }

    /* embedded newlines -- argh! */
    while ((ctmp = strchr(message, '\n')) != 0) {
        ctmp++;
        if (prefix) fputs(prefix, stderr);
        fwrite(message, 1, ctmp - message, stderr);
        message = ctmp;
    }
    /* If there were no filename or lineno, the prefix might be empty */
    if (prefix)
        fputs(prefix, stderr);
    fputs(message, stderr);

    if (!report->linebuf) {
        fputc('\n', stderr);
        goto out;
    }

    fprintf(stderr, ":\n%s%s\n%s", prefix, report->linebuf, prefix);
    n = report->tokenptr - report->linebuf;
    for (i = j = 0; i < n; i++) {
        if (report->linebuf[i] == '\t') {
            for (k = (j + 8) & ~7; j < k; j++) {
                fputc('.', stderr);
            }
            continue;
        }
        fputc('.', stderr);
        j++;
    }
    fputs("^\n", stderr);
 out:
    if (!JSREPORT_IS_WARNING(report->flags)) {
        Environment(cx)->SetExitCode(EXITCODE_RUNTIME_ERROR);
    }
    JS_free(cx, prefix);
}

JSContextCallback gOldContextCallback = NULL;

static JSBool
ContextCallback(JSContext *cx,
                uintN contextOp)
{
    if (gOldContextCallback && !gOldContextCallback(cx, contextOp))
        return JS_FALSE;

    if (contextOp == JSCONTEXT_NEW) {
        JS_SetErrorReporter(cx, ScriptErrorReporter);
        JS_SetVersion(cx, JSVERSION_LATEST);
    }
    return JS_TRUE;
}

static JSBool
Print(JSContext *cx,
      JSObject *obj,
      uintN argc,
      jsval *argv,
      jsval *rval)
{
    uintN i, n;
    JSString *str;

    for (i = n = 0; i < argc; i++) {
        str = JS_ValueToString(cx, argv[i]);
        if (!str)
            return JS_FALSE;
        fprintf(stdout, "%s%s", i ? " " : "", JS_GetStringBytes(str));
        fflush(stdout);
    }
    n++;
    if (n)
        fputc('\n', stdout);
    return JS_TRUE;
}

static JSBool
GetLine(char *bufp,
        FILE *file,
        const char *prompt)
{
    char line[256];
    fputs(prompt, stdout);
    fflush(stdout);
    if (!fgets(line, sizeof line, file))
        return JS_FALSE;
    strcpy(bufp, line);
    return JS_TRUE;
}

static JSBool
Dump(JSContext *cx,
     JSObject *obj,
     uintN argc,
     jsval *argv,
     jsval *rval)
{
    JSString *str;
    if (!argc)
        return JS_TRUE;

    str = JS_ValueToString(cx, argv[0]);
    if (!str)
        return JS_FALSE;

    fputs(JS_GetStringBytes(str), stdout);
    fflush(stdout);
    return JS_TRUE;
}

static JSBool
Load(JSContext *cx,
     JSObject *obj,
     uintN argc,
     jsval *argv,
     jsval *rval)
{
    uintN i;
    JSString *str;
    const char *filename;
    JSScript *script;
    JSBool ok;
    jsval result;
    FILE *file;

    for (i = 0; i < argc; i++) {
        str = JS_ValueToString(cx, argv[i]);
        if (!str)
            return JS_FALSE;
        argv[i] = STRING_TO_JSVAL(str);
        filename = JS_GetStringBytes(str);
        file = fopen(filename, "r");
        if (!file) {
            JS_ReportError(cx, "cannot open file '%s' for reading", filename);
            return JS_FALSE;
        }
        script = JS_CompileFileHandleForPrincipals(cx, obj, filename, file,
                                                   Environment(cx)->GetPrincipal());
        fclose(file);
        if (!script)
            return JS_FALSE;

        ok = !Environment(cx)->ShouldCompileOnly()
             ? JS_ExecuteScript(cx, obj, script, &result)
             : JS_TRUE;
        JS_DestroyScript(cx, script);
        if (!ok)
            return JS_FALSE;
    }
    return JS_TRUE;
}

static JSBool
Version(JSContext *cx,
        JSObject *obj,
        uintN argc,
        jsval *argv,
        jsval *rval)
{
    if (argc > 0 && JSVAL_IS_INT(argv[0]))
        *rval = INT_TO_JSVAL(JS_SetVersion(cx, JSVersion(JSVAL_TO_INT(argv[0]))));
    else
        *rval = INT_TO_JSVAL(JS_GetVersion(cx));
    return JS_TRUE;
}

static JSBool
BuildDate(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval)
{
    fprintf(stdout, "built on %s at %s\n", __DATE__, __TIME__);
    return JS_TRUE;
}

static JSBool
Quit(JSContext *cx,
     JSObject *obj,
     uintN argc,
     jsval *argv,
     jsval *rval)
{
    int exitCode = 0;
    JS_ConvertArguments(cx, argc, argv, "/ i", &exitCode);

    XPCShellEnvironment* env = Environment(cx);
    env->SetExitCode(exitCode);
    env->SetIsQuitting();

    return JS_FALSE;
}

static JSBool
DumpXPC(JSContext *cx,
        JSObject *obj,
        uintN argc,
        jsval *argv,
        jsval *rval)
{
    int32 depth = 2;

    if (argc > 0) {
        if (!JS_ValueToInt32(cx, argv[0], &depth))
            return JS_FALSE;
    }

    nsCOMPtr<nsIXPConnect> xpc = do_GetService(nsIXPConnect::GetCID());
    if(xpc)
        xpc->DebugDump((int16)depth);
    return JS_TRUE;
}

static JSBool
GC(JSContext *cx,
   JSObject *obj,
   uintN argc,
   jsval *argv,
   jsval *rval)
{
    JSRuntime *rt;
    uint32 preBytes;

    rt = cx->runtime;
    preBytes = rt->gcBytes;
    JS_GC(cx);
    fprintf(stdout, "before %lu, after %lu, break %08lx\n",
           (unsigned long)preBytes, (unsigned long)rt->gcBytes,
#ifdef XP_UNIX
           (unsigned long)sbrk(0)
#else
           0
#endif
           );
#ifdef JS_GCMETER
    js_DumpGCStats(rt, stdout);
#endif
    return JS_TRUE;
}

#ifdef DEBUG

static JSBool
DumpHeap(JSContext *cx,
         JSObject *obj,
         uintN argc,
         jsval *argv,
         jsval *rval)
{
    char *fileName = NULL;
    void* startThing = NULL;
    uint32 startTraceKind = 0;
    void *thingToFind = NULL;
    size_t maxDepth = (size_t)-1;
    void *thingToIgnore = NULL;
    jsval *vp;
    FILE *dumpFile;
    JSBool ok;

    vp = &argv[0];
    if (*vp != JSVAL_NULL && *vp != JSVAL_VOID) {
        JSString *str;

        str = JS_ValueToString(cx, *vp);
        if (!str)
            return JS_FALSE;
        *vp = STRING_TO_JSVAL(str);
        fileName = JS_GetStringBytes(str);
    }

    vp = &argv[1];
    if (*vp != JSVAL_NULL && *vp != JSVAL_VOID) {
        if (!JSVAL_IS_TRACEABLE(*vp))
            goto not_traceable_arg;
        startThing = JSVAL_TO_TRACEABLE(*vp);
        startTraceKind = JSVAL_TRACE_KIND(*vp);
    }

    vp = &argv[2];
    if (*vp != JSVAL_NULL && *vp != JSVAL_VOID) {
        if (!JSVAL_IS_TRACEABLE(*vp))
            goto not_traceable_arg;
        thingToFind = JSVAL_TO_TRACEABLE(*vp);
    }

    vp = &argv[3];
    if (*vp != JSVAL_NULL && *vp != JSVAL_VOID) {
        uint32 depth;

        if (!JS_ValueToECMAUint32(cx, *vp, &depth))
            return JS_FALSE;
        maxDepth = depth;
    }

    vp = &argv[4];
    if (*vp != JSVAL_NULL && *vp != JSVAL_VOID) {
        if (!JSVAL_IS_TRACEABLE(*vp))
            goto not_traceable_arg;
        thingToIgnore = JSVAL_TO_TRACEABLE(*vp);
    }

    if (!fileName) {
        dumpFile = stdout;
    } else {
        dumpFile = fopen(fileName, "w");
        if (!dumpFile) {
            fprintf(stderr, "dumpHeap: can't open %s: %s\n",
                    fileName, strerror(errno));
            return JS_FALSE;
        }
    }

    ok = JS_DumpHeap(cx, dumpFile, startThing, startTraceKind, thingToFind,
                     maxDepth, thingToIgnore);
    if (dumpFile != stdout)
        fclose(dumpFile);
    return ok;

  not_traceable_arg:
    fprintf(stderr,
            "dumpHeap: argument %u is not null or a heap-allocated thing\n",
            (unsigned)(vp - argv));
    return JS_FALSE;
}

#endif /* DEBUG */

static JSBool
Clear(JSContext *cx,
      JSObject *obj,
      uintN argc,
      jsval *argv,
      jsval *rval)
{
    if (argc > 0 && !JSVAL_IS_PRIMITIVE(argv[0])) {
        JS_ClearScope(cx, JSVAL_TO_OBJECT(argv[0]));
    } else {
        JS_ReportError(cx, "'clear' requires an object");
        return JS_FALSE;
    }
    return JS_TRUE;
}

JSFunctionSpec gGlobalFunctions[] =
{
    {"print",           Print,          0,0,0},
    {"load",            Load,           1,0,0},
    {"quit",            Quit,           0,0,0},
    {"version",         Version,        1,0,0},
    {"build",           BuildDate,      0,0,0},
    {"dumpXPC",         DumpXPC,        1,0,0},
    {"dump",            Dump,           1,0,0},
    {"gc",              GC,             0,0,0},
    {"clear",           Clear,          1,0,0},
#ifdef DEBUG
    {"dumpHeap",        DumpHeap,       5,0,0},
#endif
#ifdef MOZ_SHARK
    {"startShark",      js_StartShark,      0,0,0},
    {"stopShark",       js_StopShark,       0,0,0},
    {"connectShark",    js_ConnectShark,    0,0,0},
    {"disconnectShark", js_DisconnectShark, 0,0,0},
#endif
#ifdef MOZ_CALLGRIND
    {"startCallgrind",  js_StartCallgrind,  0,0,0},
    {"stopCallgrind",   js_StopCallgrind,   0,0,0},
    {"dumpCallgrind",   js_DumpCallgrind,   1,0,0},
#endif
    {nsnull,nsnull,0,0,0}
};

typedef enum JSShellErrNum
{
#define MSG_DEF(name, number, count, exception, format) \
    name = number,
#include "jsshell.msg"
#undef MSG_DEF
    JSShellErr_Limit
#undef MSGDEF
} JSShellErrNum;

JSErrorFormatString gErrorFormatString[JSErr_Limit] =
{
#define MSG_DEF(name, number, count, exception, format) \
    { format, count } ,
#include "jsshell.msg"
#undef MSG_DEF
};

static const JSErrorFormatString *
GetErrorMessage(void *userRef,
                const char *locale,
                const uintN errorNumber)
{
    if ((errorNumber > 0) && (errorNumber < JSShellErr_Limit))
        return &gErrorFormatString[errorNumber];

    return NULL;
}

static void
ProcessFile(JSContext *cx,
            JSObject *obj,
            const char *filename,
            FILE *file,
            JSBool forceTTY)
{
    XPCShellEnvironment* env = Environment(cx);
    XPCShellEnvironment::AutoContextPusher pusher(env);

    JSScript *script;
    jsval result;
    int lineno, startline;
    JSBool ok, hitEOF;
    char *bufp, buffer[4096];
    JSString *str;

    if (forceTTY) {
        file = stdin;
    }
    else
#ifdef HAVE_ISATTY
    if (!isatty(fileno(file)))
#endif
    {
        /*
         * It's not interactive - just execute it.
         *
         * Support the UNIX #! shell hack; gobble the first line if it starts
         * with '#'.  TODO - this isn't quite compatible with sharp variables,
         * as a legal js program (using sharp variables) might start with '#'.
         * But that would require multi-character lookahead.
         */
        int ch = fgetc(file);
        if (ch == '#') {
            while((ch = fgetc(file)) != EOF) {
                if(ch == '\n' || ch == '\r')
                    break;
            }
        }
        ungetc(ch, file);

        JSAutoRequest ar(cx);

        JSScript* script =
            JS_CompileFileHandleForPrincipals(cx, obj, filename, file,
                                              env->GetPrincipal());
        if (script) {
            if (!env->ShouldCompileOnly())
                (void)JS_ExecuteScript(cx, obj, script, &result);
            JS_DestroyScript(cx, script);
        }

        return;
    }

    /* It's an interactive filehandle; drop into read-eval-print loop. */
    lineno = 1;
    hitEOF = JS_FALSE;
    do {
        bufp = buffer;
        *bufp = '\0';

        JSAutoRequest ar(cx);

        /*
         * Accumulate lines until we get a 'compilable unit' - one that either
         * generates an error (before running out of source) or that compiles
         * cleanly.  This should be whenever we get a complete statement that
         * coincides with the end of a line.
         */
        startline = lineno;
        do {
            if (!GetLine(bufp, file, startline == lineno ? "js> " : "")) {
                hitEOF = JS_TRUE;
                break;
            }
            bufp += strlen(bufp);
            lineno++;
        } while (!JS_BufferIsCompilableUnit(cx, obj, buffer, strlen(buffer)));

        /* Clear any pending exception from previous failed compiles.  */
        JS_ClearPendingException(cx);
        script =
            JS_CompileScriptForPrincipals(cx, obj, env->GetPrincipal(), buffer,
                                          strlen(buffer), "typein", startline);
        if (script) {
            JSErrorReporter older;

            if (!env->ShouldCompileOnly()) {
                ok = JS_ExecuteScript(cx, obj, script, &result);
                if (ok && result != JSVAL_VOID) {
                    /* Suppress error reports from JS_ValueToString(). */
                    older = JS_SetErrorReporter(cx, NULL);
                    str = JS_ValueToString(cx, result);
                    JS_SetErrorReporter(cx, older);

                    if (str)
                        fprintf(stdout, "%s\n", JS_GetStringBytes(str));
                    else
                        ok = JS_FALSE;
                }
            }
            JS_DestroyScript(cx, script);
        }
    } while (!hitEOF && !env->IsQuitting());

    fprintf(stdout, "\n");
}

} /* anonymous namespace */

NS_INTERFACE_MAP_BEGIN(FullTrustSecMan)
    NS_INTERFACE_MAP_ENTRY(nsIXPCSecurityManager)
    NS_INTERFACE_MAP_ENTRY(nsIScriptSecurityManager)
    NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsIXPCSecurityManager)
NS_INTERFACE_MAP_END

NS_IMPL_ADDREF(FullTrustSecMan)
NS_IMPL_RELEASE(FullTrustSecMan)

NS_IMETHODIMP
FullTrustSecMan::CanCreateWrapper(JSContext * aJSContext,
                                  const nsIID & aIID,
                                  nsISupports *aObj,
                                  nsIClassInfo *aClassInfo,
                                  void * *aPolicy)
{
    return NS_OK;
}

NS_IMETHODIMP
FullTrustSecMan::CanCreateInstance(JSContext * aJSContext,
                                   const nsCID & aCID)
{
    return NS_OK;
}

NS_IMETHODIMP
FullTrustSecMan::CanGetService(JSContext * aJSContext,
                               const nsCID & aCID)
{
    return NS_OK;
}

NS_IMETHODIMP
FullTrustSecMan::CanAccess(PRUint32 aAction,
                           nsAXPCNativeCallContext *aCallContext,
                           JSContext * aJSContext,
                           JSObject * aJSObject,
                           nsISupports *aObj,
                           nsIClassInfo *aClassInfo,
                           jsid aName,
                           void * *aPolicy)
{
    return NS_OK;
}

NS_IMETHODIMP
FullTrustSecMan::CheckPropertyAccess(JSContext * aJSContext,
                                     JSObject * aJSObject,
                                     const char *aClassName,
                                     jsid aProperty,
                                     PRUint32 aAction)
{
    return NS_OK;
}

NS_IMETHODIMP
FullTrustSecMan::CheckLoadURIFromScript(JSContext * cx,
                                        nsIURI *uri)
{
    return NS_OK;
}

NS_IMETHODIMP
FullTrustSecMan::CheckLoadURIWithPrincipal(nsIPrincipal *aPrincipal,
                                           nsIURI *uri,
                                           PRUint32 flags)
{
    return NS_OK;
}

NS_IMETHODIMP
FullTrustSecMan::CheckLoadURI(nsIURI *from,
                              nsIURI *uri,
                              PRUint32 flags)
{
    return NS_OK;
}

NS_IMETHODIMP
FullTrustSecMan::CheckLoadURIStrWithPrincipal(nsIPrincipal *aPrincipal,
                                              const nsACString & uri,
                                              PRUint32 flags)
{
    return NS_OK;
}

NS_IMETHODIMP
FullTrustSecMan::CheckLoadURIStr(const nsACString & from,
                                 const nsACString & uri,
                                 PRUint32 flags)
{
    return NS_OK;
}

NS_IMETHODIMP
FullTrustSecMan::CheckFunctionAccess(JSContext * cx,
                                     void * funObj,
                                     void * targetObj)
{
    return NS_OK;
}

NS_IMETHODIMP
FullTrustSecMan::CanExecuteScripts(JSContext * cx,
                                   nsIPrincipal *principal,
                                   PRBool *_retval)
{
    *_retval = PR_TRUE;
    return NS_OK;
}

NS_IMETHODIMP
FullTrustSecMan::GetSubjectPrincipal(nsIPrincipal **_retval)
{
    NS_IF_ADDREF(*_retval = mSystemPrincipal);
    return *_retval ? NS_OK : NS_ERROR_FAILURE;
}

NS_IMETHODIMP
FullTrustSecMan::GetSystemPrincipal(nsIPrincipal **_retval)
{
    NS_IF_ADDREF(*_retval = mSystemPrincipal);
    return *_retval ? NS_OK : NS_ERROR_FAILURE;
}

NS_IMETHODIMP
FullTrustSecMan::GetCertificatePrincipal(const nsACString & aCertFingerprint,
                                         const nsACString & aSubjectName,
                                         const nsACString & aPrettyName,
                                         nsISupports *aCert,
                                         nsIURI *aURI,
                                         nsIPrincipal **_retval)
{
    NS_IF_ADDREF(*_retval = mSystemPrincipal);
    return *_retval ? NS_OK : NS_ERROR_FAILURE;
}

NS_IMETHODIMP
FullTrustSecMan::GetCodebasePrincipal(nsIURI *aURI,
                                      nsIPrincipal **_retval)
{
    NS_IF_ADDREF(*_retval = mSystemPrincipal);
    return *_retval ? NS_OK : NS_ERROR_FAILURE;
}

NS_IMETHODIMP
FullTrustSecMan::RequestCapability(nsIPrincipal *principal,
                                   const char *capability,
                                   PRInt16 *_retval)
{
    *_retval = nsIPrincipal::ENABLE_GRANTED;
    return NS_OK;
}

NS_IMETHODIMP
FullTrustSecMan::IsCapabilityEnabled(const char *capability,
                                     PRBool *_retval)
{
    *_retval = PR_TRUE;
    return NS_OK;
}

NS_IMETHODIMP
FullTrustSecMan::EnableCapability(const char *capability)
{
    return NS_OK;;
}

NS_IMETHODIMP
FullTrustSecMan::RevertCapability(const char *capability)
{
    return NS_OK;
}

NS_IMETHODIMP
FullTrustSecMan::DisableCapability(const char *capability)
{
    return NS_OK;
}

NS_IMETHODIMP
FullTrustSecMan::SetCanEnableCapability(const nsACString & certificateFingerprint,
                                        const char *capability,
                                        PRInt16 canEnable)
{
    return NS_OK;
}

NS_IMETHODIMP
FullTrustSecMan::GetObjectPrincipal(JSContext * cx,
                                    JSObject * obj,
                                    nsIPrincipal **_retval)
{
    NS_IF_ADDREF(*_retval = mSystemPrincipal);
    return *_retval ? NS_OK : NS_ERROR_FAILURE;
}

NS_IMETHODIMP
FullTrustSecMan::SubjectPrincipalIsSystem(PRBool *_retval)
{
    *_retval = PR_TRUE;
    return NS_OK;
}

NS_IMETHODIMP
FullTrustSecMan::CheckSameOrigin(JSContext * aJSContext,
                                 nsIURI *aTargetURI)
{
    return NS_OK;
}

NS_IMETHODIMP
FullTrustSecMan::CheckSameOriginURI(nsIURI *aSourceURI,
                                    nsIURI *aTargetURI,
                                    PRBool reportError)
{
    return NS_OK;
}

NS_IMETHODIMP
FullTrustSecMan::GetPrincipalFromContext(JSContext * cx,
                                         nsIPrincipal **_retval)
{
    NS_IF_ADDREF(*_retval = mSystemPrincipal);
    return *_retval ? NS_OK : NS_ERROR_FAILURE;
}

NS_IMETHODIMP
FullTrustSecMan::GetChannelPrincipal(nsIChannel *aChannel,
                                     nsIPrincipal **_retval)
{
    NS_IF_ADDREF(*_retval = mSystemPrincipal);
    return *_retval ? NS_OK : NS_ERROR_FAILURE;
}

NS_IMETHODIMP
FullTrustSecMan::IsSystemPrincipal(nsIPrincipal *aPrincipal,
                                   PRBool *_retval)
{
    *_retval = aPrincipal == mSystemPrincipal;
    return NS_OK;
}

NS_IMETHODIMP_(nsIPrincipal *)
FullTrustSecMan::GetCxSubjectPrincipal(JSContext *cx)
{
    return mSystemPrincipal;
}

NS_IMETHODIMP_(nsIPrincipal *)
FullTrustSecMan::GetCxSubjectPrincipalAndFrame(JSContext *cx,
                                               JSStackFrame **fp)
{
    *fp = nsnull;
    return mSystemPrincipal;
}

NS_IMETHODIMP
FullTrustSecMan::PushContextPrincipal(JSContext *cx,
                                      JSStackFrame *fp,
                                      nsIPrincipal *principal)
{
    return NS_OK;
}

NS_IMETHODIMP
FullTrustSecMan::PopContextPrincipal(JSContext *cx)
{
    return NS_OK;
}

NS_IMETHODIMP_(nsrefcnt)
XPCShellDirProvider::AddRef()
{
    return 2;
}

NS_IMETHODIMP_(nsrefcnt)
XPCShellDirProvider::Release()
{
    return 1;
}

NS_IMPL_QUERY_INTERFACE1(XPCShellDirProvider, nsIDirectoryServiceProvider)

PRBool
XPCShellDirProvider::SetGREDir(const char *dir)
{
    nsresult rv = XRE_GetFileFromPath(dir, getter_AddRefs(mGREDir));
    return NS_SUCCEEDED(rv);
}

NS_IMETHODIMP
XPCShellDirProvider::GetFile(const char *prop,
                             PRBool *persistent,
                             nsIFile* *result)
{
    if (mGREDir && !strcmp(prop, NS_GRE_DIR)) {
        *persistent = PR_TRUE;
        NS_ADDREF(*result = mGREDir);
        return NS_OK;
    }

    return NS_ERROR_FAILURE;
}

XPCShellEnvironment::
AutoContextPusher::AutoContextPusher(XPCShellEnvironment* aEnv)
{
    NS_ASSERTION(aEnv->mCx, "Null context?!");

    if (NS_SUCCEEDED(aEnv->mCxStack->Push(aEnv->mCx))) {
        mEnv = aEnv;
    }
}

XPCShellEnvironment::
AutoContextPusher::~AutoContextPusher()
{
    if (mEnv) {
        JSContext* cx;
        mEnv->mCxStack->Pop(&cx);
        NS_ASSERTION(cx == mEnv->mCx, "Wrong context on the stack!");
    }
}

// static
XPCShellEnvironment*
XPCShellEnvironment::CreateEnvironment()
{
    XPCShellEnvironment* env = new XPCShellEnvironment();
    if (env && !env->Init()) {
        delete env;
        env = nsnull;
    }
    return env;
}

XPCShellEnvironment::XPCShellEnvironment()
:   mCx(NULL),
    mJSPrincipals(NULL),
    mExitCode(0),
    mQuitting(JS_FALSE),
    mReportWarnings(JS_TRUE),
    mCompileOnly(JS_FALSE)
{
}

XPCShellEnvironment::~XPCShellEnvironment()
{
    if (mCx) {
        JS_BeginRequest(mCx);

        JSObject* global = GetGlobalObject();
        if (global) {
            JS_ClearScope(mCx, global);
        }
        mGlobalHolder.Release();

        JS_GC(mCx);

        mCxStack = nsnull;

        if (mJSPrincipals) {
            JSPRINCIPALS_DROP(mCx, mJSPrincipals);
        }

        JSRuntime* rt = gOldContextCallback ? JS_GetRuntime(mCx) : NULL;

        JS_DestroyContext(mCx);

        if (gOldContextCallback) {
            NS_ASSERTION(rt, "Should never be null!");
            JS_SetContextCallback(rt, gOldContextCallback);
            gOldContextCallback = NULL;
        }
    }
}

bool
XPCShellEnvironment::Init()
{
    nsresult rv;

#ifdef HAVE_SETBUF
    // unbuffer stdout so that output is in the correct order; note that stderr
    // is unbuffered by default
    setbuf(stdout, 0);
#endif

    nsCOMPtr<nsIJSRuntimeService> rtsvc =
        do_GetService("@mozilla.org/js/xpc/RuntimeService;1");
    if (!rtsvc) {
        NS_ERROR("failed to get nsJSRuntimeService!");
        return false;
    }

    JSRuntime *rt;
    if (NS_FAILED(rtsvc->GetRuntime(&rt)) || !rt) {
        NS_ERROR("failed to get JSRuntime from nsJSRuntimeService!");
        return false;
    }

    if (!mGlobalHolder.Hold(rt)) {
        NS_ERROR("Can't protect global object!");
        return false;
    }

    gOldContextCallback = JS_SetContextCallback(rt, ContextCallback);

    JSContext *cx = JS_NewContext(rt, 8192);
    if (!cx) {
        NS_ERROR("JS_NewContext failed!");

        JS_SetContextCallback(rt, gOldContextCallback);
        gOldContextCallback = NULL;

        return false;
    }
    mCx = cx;

    JS_SetContextPrivate(cx, this);

    nsCOMPtr<nsIXPConnect> xpc =
      do_GetService(nsIXPConnect::GetCID());
    if (!xpc) {
        NS_ERROR("failed to get nsXPConnect service!");
        return false;
    }

    nsRefPtr<FullTrustSecMan> secman(new FullTrustSecMan());
    xpc->SetSecurityManagerForJSContext(cx, secman, 0xFFFF);

    nsCOMPtr<nsIPrincipal> principal;

    nsCOMPtr<nsIScriptSecurityManager> securityManager =
        do_GetService(NS_SCRIPTSECURITYMANAGER_CONTRACTID, &rv);
    if (NS_SUCCEEDED(rv) && securityManager) {
        rv = securityManager->GetSystemPrincipal(getter_AddRefs(principal));
        if (NS_FAILED(rv)) {
            fprintf(stderr, "+++ Failed to obtain SystemPrincipal from ScriptSecurityManager service.\n");
        } else {
            // fetch the JS principals and stick in a global
            rv = principal->GetJSPrincipals(cx, &mJSPrincipals);
            if (NS_FAILED(rv)) {
                fprintf(stderr, "+++ Failed to obtain JS principals from SystemPrincipal.\n");
            }
            secman->SetSystemPrincipal(principal);
        }
    } else {
        fprintf(stderr, "+++ Failed to get ScriptSecurityManager service, running without principals");
    }

    nsCOMPtr<nsIJSContextStack> cxStack =
        do_GetService("@mozilla.org/js/xpc/ContextStack;1");
    if (!cxStack) {
        NS_ERROR("failed to get the nsThreadJSContextStack service!");
        return false;
    }
    mCxStack = cxStack;

    AutoContextPusher pusher(this);

    nsCOMPtr<nsIXPCScriptable> backstagePass;
    rv = rtsvc->GetBackstagePass(getter_AddRefs(backstagePass));
    if (NS_FAILED(rv)) {
        NS_ERROR("Failed to get backstage pass from rtsvc!");
        return false;
    }

    nsCOMPtr<nsIXPConnectJSObjectHolder> holder;
    rv = xpc->InitClassesWithNewWrappedGlobal(cx, backstagePass,
                                              NS_GET_IID(nsISupports),
                                              principal,
                                              EmptyCString(),
                                              nsIXPConnect::
                                                  FLAG_SYSTEM_GLOBAL_OBJECT,
                                              getter_AddRefs(holder));
    if (NS_FAILED(rv)) {
        NS_ERROR("InitClassesWithNewWrappedGlobal failed!");
        return false;
    }

    JSObject *globalObj;
    rv = holder->GetJSObject(&globalObj);
    if (NS_FAILED(rv)) {
        NS_ERROR("Failed to get global JSObject!");
        return false;
    }


    {
        JSAutoRequest ar(cx);

        if (!JS_DefineFunctions(cx, globalObj, gGlobalFunctions)) {
            NS_ERROR("JS_DefineFunctions failed!");
            return false;
        }
    }

    mGlobalHolder = globalObj;

    FILE* runtimeScriptFile = fopen(kDefaultRuntimeScriptFilename, "r");
    if (runtimeScriptFile) {
        fprintf(stdout, "[loading '%s'...]\n", kDefaultRuntimeScriptFilename);
        ProcessFile(cx, globalObj, kDefaultRuntimeScriptFilename,
                    runtimeScriptFile, JS_FALSE);
        fclose(runtimeScriptFile);
    }

    return true;
}

bool
XPCShellEnvironment::EvaluateString(const nsString& aString,
                                    nsString* aResult)
{
  XPCShellEnvironment* env = Environment(mCx);
  XPCShellEnvironment::AutoContextPusher pusher(env);

  JSAutoRequest ar(mCx);

  JS_ClearPendingException(mCx);

  JSObject* global = GetGlobalObject();

  JSScript* script =
      JS_CompileUCScriptForPrincipals(mCx, global, GetPrincipal(),
                                      aString.get(), aString.Length(),
                                      "typein", 0);
  if (!script) {
     return false;
  }

  if (!ShouldCompileOnly()) {
      if (aResult) {
          aResult->Truncate();
      }

      jsval result;
      JSBool ok = JS_ExecuteScript(mCx, global, script, &result);
      if (ok && result != JSVAL_VOID) {
          JSErrorReporter old = JS_SetErrorReporter(mCx, NULL);
          JSString* str = JS_ValueToString(mCx, result);
          JS_SetErrorReporter(mCx, old);

          if (str && aResult) {
              aResult->Assign(nsDependentJSString(str));
          }
      }
  }

  JS_DestroyScript(mCx, script);

  return true;
}
