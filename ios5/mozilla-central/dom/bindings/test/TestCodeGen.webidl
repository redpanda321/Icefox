/* -*- Mode: IDL; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/.
 */

interface TestExternalInterface;

interface TestNonCastableInterface {
};

enum TestEnum {
  "a",
  "b"
};

callback TestCallback = void();

TestInterface implements ImplementedInterface;

[Constructor,
 Constructor(DOMString str),
 Constructor(unsigned long num, boolean? bool),
 Constructor(TestInterface? iface),
 Constructor(TestNonCastableInterface iface)]
interface TestInterface {
  // Integer types
  // XXXbz add tests for infallible versions of all the integer stuff
  readonly attribute byte readonlyByte;
  attribute byte writableByte;
  void passByte(byte arg);
  byte receiveByte();
  void passOptionalByte(optional byte arg);
  void passOptionalByteWithDefault(optional byte arg = 0);
  void passNullableByte(byte? arg);
  void passOptionalNullableByte(optional byte? arg);

  readonly attribute short readonlyShort;
  attribute short writableShort;
  void passShort(short arg);
  short receiveShort();
  void passOptionalShort(optional short arg);
  void passOptionalShortWithDefault(optional short arg = 0);

  readonly attribute long readonlyLong;
  attribute long writableLong;
  void passLong(long arg);
  long receiveLong();
  void passOptionalLong(optional long arg);
  void passOptionalLongWithDefault(optional long arg = 0);

  readonly attribute long long readonlyLongLong;
  attribute long long writableLongLong;
  void passLongLong(long long arg);
  long long receiveLongLong();
  void passOptionalLongLong(optional long long arg);
  void passOptionalLongLongWithDefault(optional long long arg = 0);

  readonly attribute octet readonlyOctet;
  attribute octet writableOctet;
  void passOctet(octet arg);
  octet receiveOctet();
  void passOptionalOctet(optional octet arg);
  void passOptionalOctetWithDefault(optional octet arg = 0);

  readonly attribute unsigned short readonlyUnsignedShort;
  attribute unsigned short writableUnsignedShort;
  void passUnsignedShort(unsigned short arg);
  unsigned short receiveUnsignedShort();
  void passOptionalUnsignedShort(optional unsigned short arg);
  void passOptionalUnsignedShortWithDefault(optional unsigned short arg = 0);

  readonly attribute unsigned long readonlyUnsignedLong;
  attribute unsigned long writableUnsignedLong;
  void passUnsignedLong(unsigned long arg);
  unsigned long receiveUnsignedLong();
  void passOptionalUnsignedLong(optional unsigned long arg);
  void passOptionalUnsignedLongWithDefault(optional unsigned long arg = 0);

  readonly attribute unsigned long long readonlyUnsignedLongLong;
  attribute unsigned long long  writableUnsignedLongLong;
  void passUnsignedLongLong(unsigned long long arg);
  unsigned long long receiveUnsignedLongLong();
  void passOptionalUnsignedLongLong(optional unsigned long long arg);
  void passOptionalUnsignedLongLongWithDefault(optional unsigned long long arg = 0);

  // Castable interface types
  // XXXbz add tests for infallible versions of all the castable interface stuff
  TestInterface receiveSelf();
  TestInterface? receiveNullableSelf();
  TestInterface receiveWeakSelf();
  TestInterface? receiveWeakNullableSelf();
  // A verstion to test for casting to TestInterface&
  void passSelf(TestInterface arg);
  // A version we can use to test for the exact type passed in
  void passSelf2(TestInterface arg);
  void passNullableSelf(TestInterface? arg);
  attribute TestInterface nonNullSelf;
  attribute TestInterface? nullableSelf;
  // Optional arguments
  void passOptionalSelf(optional TestInterface? arg);
  void passOptionalNonNullSelf(optional TestInterface arg);
  void passOptionalSelfWithDefault(optional TestInterface? arg = null);

  // Non-wrapper-cache interface types
  [Creator]
  TestNonWrapperCacheInterface receiveNonWrapperCacheInterface();
  [Creator]
  TestNonWrapperCacheInterface? receiveNullableNonWrapperCacheInterface();
  [Creator]
  sequence<TestNonWrapperCacheInterface> receiveNonWrapperCacheInterfaceSequence();
  [Creator]
  sequence<TestNonWrapperCacheInterface?> receiveNullableNonWrapperCacheInterfaceSequence();
  [Creator]
  sequence<TestNonWrapperCacheInterface>? receiveNonWrapperCacheInterfaceNullableSequence();
  [Creator]
  sequence<TestNonWrapperCacheInterface?>? receiveNullableNonWrapperCacheInterfaceNullableSequence();

  // Non-castable interface types
  TestNonCastableInterface receiveOther();
  TestNonCastableInterface? receiveNullableOther();
  TestNonCastableInterface receiveWeakOther();
  TestNonCastableInterface? receiveWeakNullableOther();
  // A verstion to test for casting to TestNonCastableInterface&
  void passOther(TestNonCastableInterface arg);
  // A version we can use to test for the exact type passed in
  void passOther2(TestNonCastableInterface arg);
  void passNullableOther(TestNonCastableInterface? arg);
  attribute TestNonCastableInterface nonNullOther;
  attribute TestNonCastableInterface? nullableOther;
  // Optional arguments
  void passOptionalOther(optional TestNonCastableInterface? arg);
  void passOptionalNonNullOther(optional TestNonCastableInterface arg);
  void passOptionalOtherWithDefault(optional TestNonCastableInterface? arg = null);

  // External interface types
  TestExternalInterface receiveExternal();
  TestExternalInterface? receiveNullableExternal();
  TestExternalInterface receiveWeakExternal();
  TestExternalInterface? receiveWeakNullableExternal();
  // A verstion to test for casting to TestExternalInterface&
  void passExternal(TestExternalInterface arg);
  // A version we can use to test for the exact type passed in
  void passExternal2(TestExternalInterface arg);
  void passNullableExternal(TestExternalInterface? arg);
  attribute TestExternalInterface nonNullExternal;
  attribute TestExternalInterface? nullableExternal;
  // Optional arguments
  void passOptionalExternal(optional TestExternalInterface? arg);
  void passOptionalNonNullExternal(optional TestExternalInterface arg);
  void passOptionalExternalWithDefault(optional TestExternalInterface? arg = null);

  // Sequence types
  sequence<long> receiveSequence();
  sequence<long>? receiveNullableSequence();
  sequence<long?> receiveSequenceOfNullableInts();
  sequence<long?>? receiveNullableSequenceOfNullableInts();
  void passSequence(sequence<long> arg);
  void passNullableSequence(sequence<long>? arg);
  void passSequenceOfNullableInts(sequence<long?> arg);
  void passOptionalSequenceOfNullableInts(optional sequence<long?> arg);
  void passOptionalNullableSequenceOfNullableInts(optional sequence<long?>? arg);
  sequence<TestInterface> receiveCastableObjectSequence();
  sequence<TestInterface?> receiveNullableCastableObjectSequence();
  sequence<TestInterface>? receiveCastableObjectNullableSequence();
  sequence<TestInterface?>? receiveNullableCastableObjectNullableSequence();
  sequence<TestInterface> receiveWeakCastableObjectSequence();
  sequence<TestInterface?> receiveWeakNullableCastableObjectSequence();
  sequence<TestInterface>? receiveWeakCastableObjectNullableSequence();
  sequence<TestInterface?>? receiveWeakNullableCastableObjectNullableSequence();
  void passCastableObjectSequence(sequence<TestInterface> arg);
  void passNullableCastableObjectSequence(sequence<TestInterface?> arg);
  void passCastableObjectNullableSequence(sequence<TestInterface>? arg);
  void passNullableCastableObjectNullableSequence(sequence<TestInterface?>? arg);
  void passOptionalSequence(optional sequence<long> arg);
  void passOptionalNullableSequence(optional sequence<long>? arg);
  void passOptionalNullableSequenceWithDefaultValue(optional sequence<long>? arg = null);
  void passOptionalObjectSequence(optional sequence<TestInterface> arg);

  sequence<DOMString> receiveStringSequence();
  void passStringSequence(sequence<DOMString> arg);

  // Typed array types
  void passArrayBuffer(ArrayBuffer arg);
  void passNullableArrayBuffer(ArrayBuffer? arg);
  void passOptionalArrayBuffer(optional ArrayBuffer arg);
  void passOptionalNullableArrayBuffer(optional ArrayBuffer? arg);
  void passOptionalNullableArrayBufferWithDefaultValue(optional ArrayBuffer? arg= null);
  void passArrayBufferView(ArrayBufferView arg);
  void passInt8Array(Int8Array arg);
  void passInt16Array(Int16Array arg);
  void passInt32Array(Int32Array arg);
  void passUint8Array(Uint8Array arg);
  void passUint16Array(Uint16Array arg);
  void passUint32Array(Uint32Array arg);
  void passUint8ClampedArray(Uint8ClampedArray arg);
  void passFloat32Array(Float32Array arg);
  void passFloat64Array(Float64Array arg);

  // String types
  void passString(DOMString arg);
  void passNullableString(DOMString? arg);
  void passOptionalString(optional DOMString arg);
  void passOptionalNullableString(optional DOMString? arg);
  void passOptionalNullableStringWithDefaultValue(optional DOMString? arg = null);

  // Enumerated types
  void passEnum(TestEnum arg);
  // No support for nullable enums yet
  // void passNullableEnum(TestEnum? arg);
  void passOptionalEnum(optional TestEnum arg);
  // void passOptionalNullableEnum(optional TestEnum? arg);
  // void passOptionalNullableEnumWithDefaultValue(optional TestEnum? arg = null);
  TestEnum receiveEnum();
  attribute TestEnum enumAttribute;
  readonly attribute TestEnum readonlyEnumAttribute;

  // Callback types
  void passCallback(TestCallback arg);
  void passNullableCallback(TestCallback? arg);
  void passOptionalCallback(optional TestCallback arg);
  void passOptionalNullableCallback(optional TestCallback? arg);
  void passOptionalNullableCallbackWithDefaultValue(optional TestCallback? arg = null);
  TestCallback receiveCallback();
  TestCallback? receiveNullableCallback();

  // Any types
  void passAny(any arg);
  void passOptionalAny(optional any arg);
  any receiveAny();

  // object types
  void passObject(object arg);
  void passNullableObject(object? arg);
  void passOptionalObject(optional object arg);
  void passOptionalNullableObject(optional object? arg);
  void passOptionalNullableObjectWithDefaultValue(optional object? arg = null);
  object receiveObject();
  object? receiveNullableObject();

  // Union types
  void passUnion((object or long) arg);
  void passUnionWithNullable((object? or long) arg);
  void passNullableUnion((object or long)? arg);
  void passOptionalUnion(optional (object or long) arg);
  void passOptionalNullableUnion(optional (object or long)? arg);
  void passOptionalNullableUnionWithDefaultValue(optional (object or long)? arg = null);
  //void passUnionWithInterfaces((TestInterface or TestExternalInterface) arg);
  //void passUnionWithInterfacesAndNullable((TestInterface? or TestExternalInterface) arg);
  //void passUnionWithSequence((sequence<object> or long) arg);
  void passUnionWithArrayBuffer((ArrayBuffer or long) arg);
  void passUnionWithString((DOMString or object) arg);
  //void passUnionWithEnum((TestEnum or object) arg);
  void passUnionWithCallback((TestCallback or long) arg);
  void passUnionWithObject((object or long) arg);
  //void passUnionWithDict((Dict or long) arg);

  // binaryNames tests
  void methodRenamedFrom();
  void methodRenamedFrom(byte argument);
  readonly attribute byte attributeGetterRenamedFrom;
  attribute byte attributeRenamedFrom;

  void passDictionary(Dict x);
  void passOptionalDictionary(optional Dict x);
  void passNullableDictionary(Dict? x);
  void passOptionalNullableDictionary(optional Dict? x);
  void passOtherDictionary(GrandparentDict x);
  void passSequenceOfDictionaries(sequence<Dict> x);
};

interface TestNonWrapperCacheInterface {
};

interface ImplementedInterfaceParent {
  void implementedParentMethod();
  attribute boolean implementedParentProperty;

  const long implementedParentConstant = 8;
};

ImplementedInterfaceParent implements IndirectlyImplementedInterface;

interface IndirectlyImplementedInterface {
  void indirectlyImplementedMethod();
  attribute boolean indirectlyImplementedProperty;

  const long indirectlyImplementedConstant = 9;
};

interface ImplementedInterface : ImplementedInterfaceParent {
  void implementedMethod();
  attribute boolean implementedProperty;

  const long implementedConstant = 5;
};

interface DiamondImplements {
  readonly attribute long diamondImplementedProperty;
};
interface DiamondBranch1A {
};
interface DiamondBranch1B {
};
interface DiamondBranch2A : DiamondImplements {
};
interface DiamondBranch2B : DiamondImplements {
};
TestInterface implements DiamondBranch1A;
TestInterface implements DiamondBranch1B;
TestInterface implements DiamondBranch2A;
TestInterface implements DiamondBranch2B;
DiamondBranch1A implements DiamondImplements;
DiamondBranch1B implements DiamondImplements;

dictionary Dict : ParentDict {
  TestEnum someEnum;
  long x;
  long a;
  long b = 8;
  long z = 9;
  DOMString str;
};

dictionary ParentDict : GrandparentDict {
  long c = 5;
  TestInterface someInterface;
  TestExternalInterface someExternalInterface;
};
