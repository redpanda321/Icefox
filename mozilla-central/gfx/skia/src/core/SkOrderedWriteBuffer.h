
/*
 * Copyright 2011 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkOrderedWriteBuffer_DEFINED
#define SkOrderedWriteBuffer_DEFINED

#include "SkFlattenableBuffers.h"

#include "SkRefCnt.h"
#include "SkBitmap.h"
#include "SkBitmapHeap.h"
#include "SkPath.h"
#include "SkWriter32.h"

class SkFlattenable;
class SkFactorySet;
class SkNamedFactorySet;
class SkRefCntSet;

class SkOrderedWriteBuffer : public SkFlattenableWriteBuffer {
public:
    SkOrderedWriteBuffer(size_t minSize);
    SkOrderedWriteBuffer(size_t minSize, void* initialStorage,
                         size_t storageSize);
    virtual ~SkOrderedWriteBuffer();

    virtual bool isOrderedBinaryBuffer() SK_OVERRIDE { return true; }
    virtual SkOrderedWriteBuffer* getOrderedBinaryBuffer() SK_OVERRIDE { return this; }

    SkWriter32* getWriter32() { return &fWriter; }

    void writeToMemory(void* dst) { fWriter.flatten(dst); }
    uint32_t* reserve(size_t size) { return fWriter.reserve(size); }
    uint32_t size() { return fWriter.size(); }

    virtual void writeByteArray(const void* data, size_t size) SK_OVERRIDE;
    virtual void writeBool(bool value) SK_OVERRIDE;
    virtual void writeFixed(SkFixed value) SK_OVERRIDE;
    virtual void writeScalar(SkScalar value) SK_OVERRIDE;
    virtual void writeScalarArray(const SkScalar* value, uint32_t count) SK_OVERRIDE;
    virtual void writeInt(int32_t value) SK_OVERRIDE;
    virtual void writeIntArray(const int32_t* value, uint32_t count) SK_OVERRIDE;
    virtual void writeUInt(uint32_t value) SK_OVERRIDE;
    virtual void write32(int32_t value) SK_OVERRIDE;
    virtual void writeString(const char* value) SK_OVERRIDE;
    virtual void writeEncodedString(const void* value, size_t byteLength,
                                    SkPaint::TextEncoding encoding) SK_OVERRIDE;

    virtual void writeFlattenable(SkFlattenable* flattenable) SK_OVERRIDE;
    virtual void writeColor(const SkColor& color) SK_OVERRIDE;
    virtual void writeColorArray(const SkColor* color, uint32_t count) SK_OVERRIDE;
    virtual void writePoint(const SkPoint& point) SK_OVERRIDE;
    virtual void writePointArray(const SkPoint* point, uint32_t count) SK_OVERRIDE;
    virtual void writeMatrix(const SkMatrix& matrix) SK_OVERRIDE;
    virtual void writeIRect(const SkIRect& rect)SK_OVERRIDE;
    virtual void writeRect(const SkRect& rect) SK_OVERRIDE;
    virtual void writeRegion(const SkRegion& region) SK_OVERRIDE;
    virtual void writePath(const SkPath& path) SK_OVERRIDE;
    virtual size_t writeStream(SkStream* stream, size_t length) SK_OVERRIDE;

    virtual void writeBitmap(const SkBitmap& bitmap) SK_OVERRIDE;
    virtual void writeTypeface(SkTypeface* typeface) SK_OVERRIDE;

    virtual bool writeToStream(SkWStream*) SK_OVERRIDE;

    SkFactorySet* setFactoryRecorder(SkFactorySet*);
    SkNamedFactorySet* setNamedFactoryRecorder(SkNamedFactorySet*);

    SkRefCntSet* getTypefaceRecorder() const { return fTFSet; }
    SkRefCntSet* setTypefaceRecorder(SkRefCntSet*);

    void setBitmapHeap(SkBitmapHeap* bitmapHeap) {
        SkRefCnt_SafeAssign(fBitmapHeap, bitmapHeap);
    }

private:
    SkFactorySet* fFactorySet;
    SkNamedFactorySet* fNamedFactorySet;
    SkWriter32 fWriter;

    SkBitmapHeap* fBitmapHeap;
    SkRefCntSet* fTFSet;

    typedef SkFlattenableWriteBuffer INHERITED;
};

#endif // SkOrderedWriteBuffer_DEFINED
