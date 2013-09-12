/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=4 sw=4 et tw=99:
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef js_ion_type_oracle_h__
#define js_ion_type_oracle_h__

#include "jsscript.h"
#include "IonTypes.h"

namespace js {
namespace ion {
enum LazyArgumentsType {
    MaybeArguments = 0,
    DefinitelyArguments,
    NotArguments
};

class TypeOracle
{
  public:
    struct UnaryTypes {
        types::StackTypeSet *inTypes;
        types::StackTypeSet *outTypes;
    };

    struct BinaryTypes {
        types::StackTypeSet *lhsTypes;
        types::StackTypeSet *rhsTypes;
        types::StackTypeSet *outTypes;
    };

    struct Unary {
        MIRType ival;
        MIRType rval;
    };
    struct Binary {
        MIRType lhs;
        MIRType rhs;
        MIRType rval;
    };

  public:
    virtual UnaryTypes unaryTypes(UnrootedScript script, jsbytecode *pc) = 0;
    virtual BinaryTypes binaryTypes(UnrootedScript script, jsbytecode *pc) = 0;
    virtual Unary unaryOp(UnrootedScript script, jsbytecode *pc) = 0;
    virtual Binary binaryOp(UnrootedScript script, jsbytecode *pc) = 0;
    virtual types::StackTypeSet *thisTypeSet(UnrootedScript script) { return NULL; }
    virtual bool getOsrTypes(jsbytecode *osrPc, Vector<MIRType> &slotTypes) { return true; }
    virtual types::StackTypeSet *parameterTypeSet(UnrootedScript script, size_t index) { return NULL; }
    virtual types::HeapTypeSet *globalPropertyTypeSet(UnrootedScript script, jsbytecode *pc, jsid id) {
        return NULL;
    }
    virtual types::StackTypeSet *propertyRead(UnrootedScript script, jsbytecode *pc) {
        return NULL;
    }
    virtual types::StackTypeSet *propertyReadBarrier(HandleScript script, jsbytecode *pc) {
        return NULL;
    }
    virtual bool propertyReadIdempotent(HandleScript script, jsbytecode *pc, HandleId id) {
        return false;
    }
    virtual bool propertyReadAccessGetter(UnrootedScript script, jsbytecode *pc) {
        return false;
    }
    virtual types::HeapTypeSet *globalPropertyWrite(UnrootedScript script, jsbytecode *pc,
                                                jsid id, bool *canSpecialize) {
        *canSpecialize = true;
        return NULL;
    }
    virtual types::StackTypeSet *returnTypeSet(UnrootedScript script, jsbytecode *pc, types::StackTypeSet **barrier) {
        *barrier = NULL;
        return NULL;
    }
    virtual bool inObjectIsDenseArray(HandleScript script, jsbytecode *pc) {
        return false;
    }
    virtual bool inArrayIsPacked(UnrootedScript script, jsbytecode *pc) {
        return false;
    }
    virtual bool elementReadIsDenseArray(UnrootedScript script, jsbytecode *pc) {
        return false;
    }
    virtual bool elementReadIsTypedArray(UnrootedScript script, jsbytecode *pc, int *arrayType) {
        return false;
    }
    virtual bool elementReadIsString(UnrootedScript script, jsbytecode *pc) {
        return false;
    }
    virtual bool elementReadIsPacked(UnrootedScript script, jsbytecode *pc) {
        return false;
    }
    virtual void elementReadGeneric(UnrootedScript script, jsbytecode *pc, bool *cacheable, bool *monitorResult) {
        *cacheable = false;
        *monitorResult = true;
    }
    virtual bool setElementHasWrittenHoles(UnrootedScript script, jsbytecode *pc) {
        return true;
    }
    virtual bool elementWriteIsDenseArray(HandleScript script, jsbytecode *pc) {
        return false;
    }
    virtual bool elementWriteIsTypedArray(UnrootedScript script, jsbytecode *pc, int *arrayType) {
        return false;
    }
    virtual bool elementWriteIsPacked(UnrootedScript script, jsbytecode *pc) {
        return false;
    }
    virtual bool propertyWriteCanSpecialize(UnrootedScript script, jsbytecode *pc) {
        return true;
    }
    virtual bool propertyWriteNeedsBarrier(UnrootedScript script, jsbytecode *pc, jsid id) {
        return true;
    }
    virtual bool elementWriteNeedsBarrier(UnrootedScript script, jsbytecode *pc) {
        return true;
    }
    virtual MIRType elementWrite(UnrootedScript script, jsbytecode *pc) {
        return MIRType_None;
    }
    virtual bool arrayPrototypeHasIndexedProperty() {
        return true;
    }
    virtual bool canInlineCalls() {
        return false;
    }

    /* |pc| must be a |JSOP_CALL|. */
    virtual types::StackTypeSet *getCallTarget(UnrootedScript caller, uint32_t argc, jsbytecode *pc) {
        // Same assertion as TypeInferenceOracle::getCallTarget.
        JS_ASSERT(js_CodeSpec[*pc].format & JOF_INVOKE && JSOp(*pc) != JSOP_EVAL);
        return NULL;
    }
    virtual types::StackTypeSet *getCallArg(UnrootedScript script, uint32_t argc, uint32_t arg, jsbytecode *pc) {
        return NULL;
    }
    virtual types::StackTypeSet *getCallReturn(UnrootedScript script, jsbytecode *pc) {
        return NULL;
    }
    virtual bool canInlineCall(HandleScript caller, jsbytecode *pc) {
        return false;
    }
    virtual bool canEnterInlinedFunction(JSFunction *callee) {
        return false;
    }

    virtual LazyArgumentsType isArgumentObject(types::StackTypeSet *obj) {
        return MaybeArguments;
    }
    virtual LazyArgumentsType propertyReadMagicArguments(UnrootedScript script, jsbytecode *pc) {
        return MaybeArguments;
    }
    virtual LazyArgumentsType elementReadMagicArguments(UnrootedScript script, jsbytecode *pc) {
        return MaybeArguments;
    }
    virtual LazyArgumentsType elementWriteMagicArguments(UnrootedScript script, jsbytecode *pc) {
        return MaybeArguments;
    }
    virtual types::StackTypeSet *aliasedVarBarrier(UnrootedScript script, jsbytecode *pc,
                                                   types::StackTypeSet **barrier)
    {
        return NULL;
    }
};

class DummyOracle : public TypeOracle
{
  public:
    UnaryTypes unaryTypes(UnrootedScript script, jsbytecode *pc) {
        UnaryTypes u;
        u.inTypes = NULL;
        u.outTypes = NULL;
        return u;
    }
    BinaryTypes binaryTypes(UnrootedScript script, jsbytecode *pc) {
        BinaryTypes b;
        b.lhsTypes = NULL;
        b.rhsTypes = NULL;
        b.outTypes = NULL;
        return b;
    }
    Unary unaryOp(UnrootedScript script, jsbytecode *pc) {
        Unary u;
        u.ival = MIRType_Int32;
        u.rval = MIRType_Int32;
        return u;
    }
    Binary binaryOp(UnrootedScript script, jsbytecode *pc) {
        Binary b;
        b.lhs = MIRType_Int32;
        b.rhs = MIRType_Int32;
        b.rval = MIRType_Int32;
        return b;
    }
};

class TypeInferenceOracle : public TypeOracle
{
    JSContext *cx;
    HeapPtrScript script_;

    MIRType getMIRType(types::StackTypeSet *types);
    MIRType getMIRType(types::HeapTypeSet *types);

  public:
    TypeInferenceOracle() : cx(NULL), script_(NULL) {}

    bool init(JSContext *cx, HandleScript script);

    UnrootedScript script() { return script_.get(); }

    UnaryTypes unaryTypes(UnrootedScript script, jsbytecode *pc);
    BinaryTypes binaryTypes(UnrootedScript script, jsbytecode *pc);
    Unary unaryOp(UnrootedScript script, jsbytecode *pc);
    Binary binaryOp(UnrootedScript script, jsbytecode *pc);
    types::StackTypeSet *thisTypeSet(UnrootedScript script);
    bool getOsrTypes(jsbytecode *osrPc, Vector<MIRType> &slotTypes);
    types::StackTypeSet *parameterTypeSet(UnrootedScript script, size_t index);
    types::HeapTypeSet *globalPropertyTypeSet(UnrootedScript script, jsbytecode *pc, jsid id);
    types::StackTypeSet *propertyRead(UnrootedScript script, jsbytecode *pc);
    types::StackTypeSet *propertyReadBarrier(HandleScript script, jsbytecode *pc);
    bool propertyReadIdempotent(HandleScript script, jsbytecode *pc, HandleId id);
    bool propertyReadAccessGetter(UnrootedScript script, jsbytecode *pc);
    types::HeapTypeSet *globalPropertyWrite(UnrootedScript script, jsbytecode *pc, jsid id, bool *canSpecialize);
    types::StackTypeSet *returnTypeSet(UnrootedScript script, jsbytecode *pc, types::StackTypeSet **barrier);
    types::StackTypeSet *getCallTarget(UnrootedScript caller, uint32_t argc, jsbytecode *pc);
    types::StackTypeSet *getCallArg(UnrootedScript caller, uint32_t argc, uint32_t arg, jsbytecode *pc);
    types::StackTypeSet *getCallReturn(UnrootedScript caller, jsbytecode *pc);
    bool inObjectIsDenseArray(HandleScript script, jsbytecode *pc);
    bool inArrayIsPacked(UnrootedScript script, jsbytecode *pc);
    bool elementReadIsDenseArray(UnrootedScript script, jsbytecode *pc);
    bool elementReadIsTypedArray(UnrootedScript script, jsbytecode *pc, int *atype);
    bool elementReadIsString(UnrootedScript script, jsbytecode *pc);
    bool elementReadIsPacked(UnrootedScript script, jsbytecode *pc);
    void elementReadGeneric(UnrootedScript script, jsbytecode *pc, bool *cacheable, bool *monitorResult);
    bool elementWriteIsDenseArray(HandleScript script, jsbytecode *pc);
    bool elementWriteIsTypedArray(UnrootedScript script, jsbytecode *pc, int *arrayType);
    bool elementWriteIsPacked(UnrootedScript script, jsbytecode *pc);
    bool setElementHasWrittenHoles(UnrootedScript script, jsbytecode *pc);
    bool propertyWriteCanSpecialize(UnrootedScript script, jsbytecode *pc);
    bool propertyWriteNeedsBarrier(UnrootedScript script, jsbytecode *pc, jsid id);
    bool elementWriteNeedsBarrier(UnrootedScript script, jsbytecode *pc);
    MIRType elementWrite(UnrootedScript script, jsbytecode *pc);
    bool arrayPrototypeHasIndexedProperty();
    bool canInlineCalls();
    bool canInlineCall(HandleScript caller, jsbytecode *pc);
    bool canEnterInlinedFunction(JSFunction *callee);
    types::StackTypeSet *aliasedVarBarrier(UnrootedScript script, jsbytecode *pc, types::StackTypeSet **barrier);

    LazyArgumentsType isArgumentObject(types::StackTypeSet *obj);
    LazyArgumentsType propertyReadMagicArguments(UnrootedScript script, jsbytecode *pc);
    LazyArgumentsType elementReadMagicArguments(UnrootedScript script, jsbytecode *pc);
    LazyArgumentsType elementWriteMagicArguments(UnrootedScript script, jsbytecode *pc);
};

static inline MIRType
MIRTypeFromValueType(JSValueType type)
{
    switch (type) {
      case JSVAL_TYPE_DOUBLE:
        return MIRType_Double;
      case JSVAL_TYPE_INT32:
        return MIRType_Int32;
      case JSVAL_TYPE_UNDEFINED:
        return MIRType_Undefined;
      case JSVAL_TYPE_STRING:
        return MIRType_String;
      case JSVAL_TYPE_BOOLEAN:
        return MIRType_Boolean;
      case JSVAL_TYPE_NULL:
        return MIRType_Null;
      case JSVAL_TYPE_OBJECT:
        return MIRType_Object;
      case JSVAL_TYPE_MAGIC:
        return MIRType_Magic;
      case JSVAL_TYPE_UNKNOWN:
        return MIRType_Value;
      default:
        JS_NOT_REACHED("unexpected jsval type");
        return MIRType_None;
    }
}

static inline JSValueType
ValueTypeFromMIRType(MIRType type)
{
  switch (type) {
    case MIRType_Undefined:
      return JSVAL_TYPE_UNDEFINED;
    case MIRType_Null:
      return JSVAL_TYPE_NULL;
    case MIRType_Boolean:
      return JSVAL_TYPE_BOOLEAN;
    case MIRType_Int32:
      return JSVAL_TYPE_INT32;
    case MIRType_Double:
      return JSVAL_TYPE_DOUBLE;
    case MIRType_String:
      return JSVAL_TYPE_STRING;
    case MIRType_Magic:
      return JSVAL_TYPE_MAGIC;
    default:
      JS_ASSERT(type == MIRType_Object);
      return JSVAL_TYPE_OBJECT;
  }
}

static inline JSValueTag
MIRTypeToTag(MIRType type)
{
    return JSVAL_TYPE_TO_TAG(ValueTypeFromMIRType(type));
}

static inline const char *
StringFromMIRType(MIRType type)
{
  switch (type) {
    case MIRType_Undefined:
      return "Undefined";
    case MIRType_Null:
      return "Null";
    case MIRType_Boolean:
      return "Bool";
    case MIRType_Int32:
      return "Int32";
    case MIRType_Double:
      return "Double";
    case MIRType_String:
      return "String";
    case MIRType_Object:
      return "Object";
    case MIRType_Magic:
      return "Magic";
    case MIRType_Value:
      return "Value";
    case MIRType_None:
      return "None";
    case MIRType_Slots:
      return "Slots";
    case MIRType_Elements:
      return "Elements";
    case MIRType_StackFrame:
      return "StackFrame";
    default:
      JS_NOT_REACHED("Unknown MIRType.");
      return "";
  }
}

static inline bool
IsNumberType(MIRType type)
{
    return type == MIRType_Int32 || type == MIRType_Double;
}

static inline bool
IsNullOrUndefined(MIRType type)
{
    return type == MIRType_Null || type == MIRType_Undefined;
}

} /* ion */
} /* js */

#endif // js_ion_type_oracle_h__

