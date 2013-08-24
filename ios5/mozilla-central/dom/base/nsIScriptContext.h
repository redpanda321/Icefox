/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsIScriptContext_h__
#define nsIScriptContext_h__

#include "nscore.h"
#include "nsStringGlue.h"
#include "nsISupports.h"
#include "nsCOMPtr.h"
#include "nsIProgrammingLanguage.h"
#include "jsfriendapi.h"
#include "jspubtd.h"

class nsIScriptGlobalObject;
class nsIScriptSecurityManager;
class nsIPrincipal;
class nsIAtom;
class nsIArray;
class nsIVariant;
class nsIObjectInputStream;
class nsIObjectOutputStream;
template<class> class nsScriptObjectHolder;
class nsIScriptObjectPrincipal;
class nsIDOMWindow;
class nsIURI;

typedef void (*nsScriptTerminationFunc)(nsISupports* aRef);

#define NS_ISCRIPTCONTEXTPRINCIPAL_IID \
  { 0xd012cdb3, 0x8f1e, 0x4440, \
    { 0x8c, 0xbd, 0x32, 0x7f, 0x98, 0x1d, 0x37, 0xb4 } }

class nsIScriptContextPrincipal : public nsISupports
{
public:
  NS_DECLARE_STATIC_IID_ACCESSOR(NS_ISCRIPTCONTEXTPRINCIPAL_IID)

  virtual nsIScriptObjectPrincipal* GetObjectPrincipal() = 0;
};

NS_DEFINE_STATIC_IID_ACCESSOR(nsIScriptContextPrincipal,
                              NS_ISCRIPTCONTEXTPRINCIPAL_IID)

#define NS_ISCRIPTCONTEXT_IID \
{ 0x9a4df96d, 0xa231, 0x4108, \
  { 0xb5, 0xbc, 0xaf, 0x67, 0x7a, 0x36, 0xa7, 0x44 } }

/* This MUST match JSVERSION_DEFAULT.  This version stuff if we don't
   know what language we have is a little silly... */
#define SCRIPTVERSION_DEFAULT JSVERSION_DEFAULT

/**
 * It is used by the application to initialize a runtime and run scripts.
 * A script runtime would implement this interface.
 */
class nsIScriptContext : public nsIScriptContextPrincipal
{
public:
  NS_DECLARE_STATIC_IID_ACCESSOR(NS_ISCRIPTCONTEXT_IID)

  virtual void SetGlobalObject(nsIScriptGlobalObject* aGlobalObject) = 0;

  /**
   * Compile and execute a script.
   *
   * @param aScript a string representing the script to be executed
   * @param aScopeObject a script object for the scope to execute in, or
   *                     nsnull to use a default scope
   * @param aPrincipal the principal the script should be evaluated with
   * @param aOriginPrincipal the principal the script originates from.  If null,
   *                         aPrincipal is used.
   * @param aURL the URL or filename for error messages
   * @param aLineNo the starting line number of the script for error messages
   * @param aVersion the script language version to use when executing
   * @param aRetValue the result of executing the script, or null for no result.
   *        If this is a JS context, it's the caller's responsibility to
   *        preserve aRetValue from GC across this call
   * @param aIsUndefined true if the result of executing the script is the
   *                     undefined value
   *
   * @return NS_OK if the script was valid and got executed
   *
   **/
  virtual nsresult EvaluateString(const nsAString& aScript,
                                  JSObject* aScopeObject,
                                  nsIPrincipal *aPrincipal,
                                  nsIPrincipal *aOriginPrincipal,
                                  const char *aURL,
                                  PRUint32 aLineNo,
                                  JSVersion aVersion,
                                  nsAString *aRetValue,
                                  bool* aIsUndefined) = 0;

  virtual nsresult EvaluateStringWithValue(const nsAString& aScript,
                                           JSObject* aScopeObject,
                                           nsIPrincipal *aPrincipal,
                                           const char *aURL,
                                           PRUint32 aLineNo,
                                           PRUint32 aVersion,
                                           JS::Value* aRetValue,
                                           bool* aIsUndefined) = 0;

  /**
   * Compile a script.
   *
   * @param aText a PRUnichar buffer containing script source
   * @param aTextLength number of characters in aText
   * @param aPrincipal the principal that produced the script
   * @param aURL the URL or filename for error messages
   * @param aLineNo the starting line number of the script for error messages
   * @param aVersion the script language version to use when executing
   * @param aScriptObject an executable object that's the result of compiling
   *                      the script.
   *
   * @return NS_OK if the script source was valid and got compiled.
   *
   **/
  virtual nsresult CompileScript(const PRUnichar* aText,
                                 PRInt32 aTextLength,
                                 nsIPrincipal* aPrincipal,
                                 const char* aURL,
                                 PRUint32 aLineNo,
                                 PRUint32 aVersion,
                                 nsScriptObjectHolder<JSScript>& aScriptObject) = 0;

  /**
   * Execute a precompiled script object.
   *
   * @param aScriptObject an object representing the script to be executed
   * @param aScopeObject an object telling the scope in which to execute,
   *                     or nsnull to use a default scope
   * @param aRetValue the result of executing the script, may be null in
   *                  which case no result string is computed
   * @param aIsUndefined true if the result of executing the script is the
   *                     undefined value, may be null for "don't care"
   *
   * @return NS_OK if the script was valid and got executed
   *
   */
  virtual nsresult ExecuteScript(JSScript* aScriptObject,
                                 JSObject* aScopeObject,
                                 nsAString* aRetValue,
                                 bool* aIsUndefined) = 0;

  /**
   * Compile the event handler named by atom aName, with function body aBody
   * into a function object returned if ok via aHandler.  Does NOT bind the
   * function to anything - BindCompiledEventHandler() should be used for that
   * purpose.  Note that this event handler is always considered 'shared' and
   * hence is compiled without principals.  Never call the returned object
   * directly - it must be bound (and thereby cloned, and therefore have the 
   * correct principals) before use!
   *
   * If the compilation sets a pending exception on the native context, it is
   * this method's responsibility to report said exception immediately, without
   * relying on callers to do so.
   *
   *
   * @param aName an nsIAtom pointer naming the function; it must be lowercase
   *        and ASCII, and should not be longer than 63 chars.  This bound on
   *        length is enforced only by assertions, so caveat caller!
   * @param aEventName the name that the event object should be bound to
   * @param aBody the event handler function's body
   * @param aURL the URL or filename for error messages
   * @param aLineNo the starting line number of the script for error messages
   * @param aVersion the script language version to use when executing
   * @param aHandler the out parameter in which a void pointer to the compiled
   *        function object is stored on success
   *
   * @return NS_OK if the function body was valid and got compiled
   */
  virtual nsresult CompileEventHandler(nsIAtom* aName,
                                       PRUint32 aArgCount,
                                       const char** aArgNames,
                                       const nsAString& aBody,
                                       const char* aURL,
                                       PRUint32 aLineNo,
                                       PRUint32 aVersion,
                                       nsScriptObjectHolder<JSObject>& aHandler) = 0;

  /**
   * Call the function object with given args and return its boolean result,
   * or true if the result isn't boolean.
   *
   * @param aTarget the event target
   * @param aScript an object telling the scope in which to call the compiled
   *        event handler function.
   * @param aHandler function object (function and static scope) to invoke.
   * @param argv array of arguments.  Note each element is assumed to
   *        be an nsIVariant.
   * @param rval out parameter returning result
   **/
  virtual nsresult CallEventHandler(nsISupports* aTarget,
                                    JSObject* aScope, JSObject* aHandler,
                                    nsIArray *argv, nsIVariant **rval) = 0;

  /**
   * Bind an already-compiled event handler function to the given
   * target.  Scripting languages with static scoping must re-bind the
   * scope chain for aHandler to begin (after the activation scope for
   * aHandler itself, typically) with aTarget's scope.
   *
   * The result of the bind operation is a new handler object, with
   * principals now set and scope set as above.  This is returned in
   * aBoundHandler.  When this function is called, aBoundHandler is
   * expected to not be holding an object.
   *
   * @param aTarget an object telling the scope in which to bind the compiled
   *        event handler function.  The context will presumably associate
   *        this nsISupports with a native script object.
   * @param aScope the scope in which the script object for aTarget should be
   *        looked for.
   * @param aHandler the function object to bind, created by an earlier call to
   *        CompileEventHandler
   * @param aBoundHandler [out] the result of the bind operation.
   * @return NS_OK if the function was successfully bound
   */
  virtual nsresult BindCompiledEventHandler(nsISupports* aTarget,
                                            JSObject* aScope,
                                            JSObject* aHandler,
                                            nsScriptObjectHolder<JSObject>& aBoundHandler) = 0;

  /**
   * Compile a function that isn't used as an event handler.
   *
   * NOTE: Not yet language agnostic (main problem is XBL - not yet agnostic)
   * Caller must make sure aFunctionObject is a JS GC root.
   *
   **/
  virtual nsresult CompileFunction(JSObject* aTarget,
                                   const nsACString& aName,
                                   PRUint32 aArgCount,
                                   const char** aArgArray,
                                   const nsAString& aBody,
                                   const char* aURL,
                                   PRUint32 aLineNo,
                                   PRUint32 aVersion,
                                   bool aShared,
                                   JSObject** aFunctionObject) = 0;

  /**
   * Return the global object.
   *
   **/
  virtual nsIScriptGlobalObject *GetGlobalObject() = 0;

  /**
   * Return the native script context
   *
   **/
  virtual JSContext* GetNativeContext() = 0;

  /**
   * Return the native global object for this context.
   *
   **/
  virtual JSObject* GetNativeGlobal() = 0;

  /**
   * Initialize the context generally. Does not create a global object.
   **/
  virtual nsresult InitContext() = 0;

  /**
   * Check to see if context is as yet intialized. Used to prevent
   * reentrancy issues during the initialization process.
   *
   * @return true if initialized, false if not
   *
   */
  virtual bool IsContextInitialized() = 0;

  /**
   * For garbage collected systems, do a synchronous collection pass.
   * May be a no-op on other systems
   *
   * @return NS_OK if the method is successful
   */
  virtual void GC(js::gcreason::Reason aReason) = 0;

  /**
   * Inform the context that a script was evaluated.
   * A GC may be done if "necessary."
   * This call is necessary if script evaluation is done
   * without using the EvaluateScript method.
   * @param aTerminated If true then call termination function if it was 
   *    previously set. Within DOM this will always be true, but outside 
   *    callers (such as xpconnect) who may do script evaluations nested
   *    inside DOM script evaluations can pass false to avoid premature
   *    calls to the termination function.
   * @return NS_OK if the method is successful
   */
  virtual void ScriptEvaluated(bool aTerminated) = 0;

  virtual nsresult Serialize(nsIObjectOutputStream* aStream,
                             JSScript* aScriptObject) = 0;
  
  /* Deserialize a script from a stream.
   */
  virtual nsresult Deserialize(nsIObjectInputStream* aStream,
                               nsScriptObjectHolder<JSScript>& aResult) = 0;

  /**
   * JS only - this function need not be implemented by languages other
   * than JS (ie, this should be moved to a private interface!)
   * Called to specify a function that should be called when the current
   * script (if there is one) terminates. Generally used if breakdown
   * of script state needs to happen, but should be deferred till
   * the end of script evaluation.
   *
   * @throws NS_ERROR_OUT_OF_MEMORY if that happens
   */
  virtual void SetTerminationFunction(nsScriptTerminationFunc aFunc,
                                      nsIDOMWindow* aRef) = 0;

  /**
   * Called to disable/enable script execution in this context.
   */
  virtual bool GetScriptsEnabled() = 0;
  virtual void SetScriptsEnabled(bool aEnabled, bool aFireTimeouts) = 0;

  // SetProperty is suspect and jst believes should not be needed.  Currenly
  // used only for "arguments".
  virtual nsresult SetProperty(JSObject* aTarget, const char* aPropName, nsISupports* aVal) = 0;
  /** 
   * Called to set/get information if the script context is
   * currently processing a script tag
   */
  virtual bool GetProcessingScriptTag() = 0;
  virtual void SetProcessingScriptTag(bool aResult) = 0;

  /**
   * Called to find out if this script context might be executing script.
   */
  virtual bool GetExecutingScript() = 0;

  /**
   * Tell the context whether or not to GC when destroyed.  An optimization
   * used when the window is a [i]frame, so GC will happen anyway.
   */
  virtual void SetGCOnDestruction(bool aGCOnDestruction) = 0;

  /**
   * Initialize DOM classes on aGlobalObj, always call
   * WillInitializeContext() before calling InitContext(), and always
   * call DidInitializeContext() when a context is fully
   * (successfully) initialized.
   */
  virtual nsresult InitClasses(JSObject* aGlobalObj) = 0;

  /**
   * Tell the context we're about to be reinitialize it.
   */
  virtual void WillInitializeContext() = 0;

  /**
   * Tell the context we're done reinitializing it.
   */
  virtual void DidInitializeContext() = 0;

  /* Memory managment for script objects.  Used by the implementation of
   * nsScriptObjectHolder to manage the lifetimes of the held script objects.
   *
   * See also nsIScriptRuntime, which has identical methods and is useful
   * in situations when you do not have an nsIScriptContext.
   * 
   */
  virtual nsresult DropScriptObject(void *object) = 0;
  virtual nsresult HoldScriptObject(void *object) = 0;

  virtual void EnterModalState() = 0;
  virtual void LeaveModalState() = 0;
};

NS_DEFINE_STATIC_IID_ACCESSOR(nsIScriptContext, NS_ISCRIPTCONTEXT_IID)

#endif // nsIScriptContext_h__

