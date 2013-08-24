/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * vim: sw=2 ts=8 et :
 */
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
 * The Original Code is Mozilla IPC.
 *
 * The Initial Developer of the Original Code is
 *   The Mozilla Foundation
 * Portions created by the Initial Developer are Copyright (C) 2009
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Chris Jones <jones.chris.g@gmail.com>
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

#include <math.h>

#include "Shmem.h"

#include "ProtocolUtils.h"
#include "SharedMemoryBasic.h"
#include "SharedMemorySysV.h"

#include "nsAutoPtr.h"


namespace mozilla {
namespace ipc {

class ShmemCreated : public IPC::Message
{
private:
  typedef Shmem::id_t id_t;

public:
  ShmemCreated(int32 routingId,
               const id_t& aIPDLId,
               const size_t& aSize,
               const SharedMemoryBasic::Handle& aHandle) :
    IPC::Message(routingId, SHMEM_CREATED_MESSAGE_TYPE, PRIORITY_NORMAL)
  {
    IPC::WriteParam(this, aIPDLId);
    IPC::WriteParam(this, aSize);
    IPC::WriteParam(this, int32(SharedMemory::TYPE_BASIC)),
    IPC::WriteParam(this, aHandle);
  }

  // Instead of a single Read() function, we have ReadInfo() and
  // ReadHandle().  The reason is that the handle type is specific to
  // the shmem type.  These functions should only be called in the
  // order ReadInfo(); ReadHandle();, and only once each.

  static bool
  ReadInfo(const Message* msg, void** iter,
           id_t* aIPDLId,
           size_t* aSize,
           SharedMemory::SharedMemoryType* aType)
  {
    if (!IPC::ReadParam(msg, iter, aIPDLId) ||
        !IPC::ReadParam(msg, iter, aSize) ||
        !IPC::ReadParam(msg, iter, reinterpret_cast<int32*>(aType)))
      return false;
    return true;
  }

  static bool
  ReadHandle(const Message* msg, void** iter,
             SharedMemoryBasic::Handle* aHandle)
  {
    if (!IPC::ReadParam(msg, iter, aHandle))
      return false;
    msg->EndRead(*iter);
    return true;
  }

#ifdef MOZ_HAVE_SHAREDMEMORYSYSV
  ShmemCreated(int32 routingId,
               const id_t& aIPDLId,
               const size_t& aSize,
               const SharedMemorySysV::Handle& aHandle) :
    IPC::Message(routingId, SHMEM_CREATED_MESSAGE_TYPE, PRIORITY_NORMAL)
  {
    IPC::WriteParam(this, aIPDLId);
    IPC::WriteParam(this, aSize);
    IPC::WriteParam(this, int32(SharedMemory::TYPE_SYSV)),
    IPC::WriteParam(this, aHandle);
  }

  static bool
  ReadHandle(const Message* msg, void** iter,
             SharedMemorySysV::Handle* aHandle)
  {
    if (!IPC::ReadParam(msg, iter, aHandle))
      return false;
    msg->EndRead(*iter);
    return true;
  }
#endif

  void Log(const std::string& aPrefix,
           FILE* aOutf) const
  {
    fputs("(special ShmemCreated msg)", aOutf);
  }
};

class ShmemDestroyed : public IPC::Message
{
private:
  typedef Shmem::id_t id_t;

public:
  ShmemDestroyed(int32 routingId,
                 const id_t& aIPDLId) :
    IPC::Message(routingId, SHMEM_DESTROYED_MESSAGE_TYPE, PRIORITY_NORMAL)
  {
    IPC::WriteParam(this, aIPDLId);
  }
};


#ifdef MOZ_HAVE_SHAREDMEMORYSYSV
static Shmem::SharedMemory*
CreateSegment(size_t aNBytes, SharedMemorySysV::Handle aHandle)
{
  nsAutoPtr<SharedMemory> segment;

  if (SharedMemorySysV::IsHandleValid(aHandle)) {
    segment = new SharedMemorySysV(aHandle);
  }
  else {
    segment = new SharedMemorySysV();

    if (!segment->Create(aNBytes))
      return 0;
  }
  if (!segment->Map(aNBytes))
    return 0;

  segment->AddRef();
  return segment.forget();
}
#endif

static Shmem::SharedMemory*
CreateSegment(size_t aNBytes, SharedMemoryBasic::Handle aHandle)
{
  nsAutoPtr<SharedMemory> segment;

  if (SharedMemoryBasic::IsHandleValid(aHandle)) {
    segment = new SharedMemoryBasic(aHandle);
  }
  else {
    segment = new SharedMemoryBasic();

    if (!segment->Create(aNBytes))
      return 0;
  }
  if (!segment->Map(aNBytes))
    return 0;

  segment->AddRef();
  return segment.forget();
}

static void
DestroySegment(SharedMemory* aSegment)
{
  // the SharedMemory dtor closes and unmaps the actual OS shmem segment
  if (aSegment)
    aSegment->Release();
}

static size_t
PageAlignedSize(size_t aSize)
{
  size_t pageSize = SharedMemory::SystemPageSize();
  size_t nPagesNeeded = int(ceil(double(aSize) / double(pageSize)));
  return pageSize * nPagesNeeded;
}


#if defined(DEBUG)

static const char sMagic[] =
    "This little piggy went to market.\n"
    "This little piggy stayed at home.\n"
    "This little piggy has roast beef,\n"
    "This little piggy had none.\n"
    "And this little piggy cried \"Wee! Wee! Wee!\" all the way home";


struct Header {
  size_t mSize;
  char mMagic[sizeof(sMagic)];
};

static void
GetSections(Shmem::SharedMemory* aSegment,
            char** aFrontSentinel,
            char** aData,
            char** aBackSentinel)
{
  NS_ABORT_IF_FALSE(aSegment && aFrontSentinel && aData && aBackSentinel,
                    "NULL param(s)");

  *aFrontSentinel = reinterpret_cast<char*>(aSegment->memory());
  NS_ABORT_IF_FALSE(*aFrontSentinel, "NULL memory()");

  size_t pageSize = Shmem::SharedMemory::SystemPageSize();
  *aData = *aFrontSentinel + pageSize;

  *aBackSentinel = *aFrontSentinel + aSegment->Size() - pageSize;
}

static void
Protect(SharedMemory* aSegment)
{
  NS_ABORT_IF_FALSE(aSegment, "NULL segment");
  aSegment->Protect(reinterpret_cast<char*>(aSegment->memory()),
                    aSegment->Size(),
                    RightsNone);
}

static void
Unprotect(SharedMemory* aSegment)
{
  NS_ABORT_IF_FALSE(aSegment, "NULL segment");
  aSegment->Protect(reinterpret_cast<char*>(aSegment->memory()),
                    aSegment->Size(),
                    RightsRead | RightsWrite);
}

//
// In debug builds, we specially allocate shmem segments.  The layout
// is as follows
//
//   Page 0: "front sentinel"
//     size of mapping
//     magic bytes
//   Page 1 through n-1:
//     user data
//   Page n: "back sentinel"
//     [nothing]
//
// The mapping can be in one of the following states, wrt to the
// current process.
//
//   State "unmapped": all pages are mapped with no access rights.
//
//   State "mapping": all pages are mapped with read/write access.
//
//   State "mapped": the front and back sentinels are mapped with no
//     access rights, and all the other pages are mapped with
//     read/write access.
//
// When a SharedMemory segment is first allocated, it starts out in
// the "mapping" state for the process that allocates the segment, and
// in the "unmapped" state for the other process.  The allocating
// process will then create a Shmem, which takes the segment into the
// "mapped" state, where it can be accessed by clients.
//
// When a Shmem is sent to another process in an IPDL message, the
// segment transitions into the "unmapped" state for the sending
// process, and into the "mapping" state for the receiving process.
// The receiving process will then create a Shmem from the underlying
// segment, and take the segment into the "mapped" state.
//
// In the "mapping" state, we use the front sentinel to verify the
// integrity of the shmem segment.  If valid, it has a size_t
// containing the number of bytes the user allocated followed by the
// magic bytes above.
//
// In the "mapped" state, the front and back sentinels have no access
// rights.  They act as guards against buffer overflows and underflows
// in client code; if clients touch a sentinel, they die with SIGSEGV.
//
// The "unmapped" state is used to enforce single-owner semantics of
// the shmem segment.  If a process other than the current owner tries
// to touch the segment, it dies with SIGSEGV.
//

Shmem::Shmem(IHadBetterBeIPDLCodeCallingThis_OtherwiseIAmADoodyhead,
             SharedMemory* aSegment, id_t aId) :
    mSegment(aSegment),
    mData(0),
    mSize(0)
{
  NS_ABORT_IF_FALSE(mSegment, "NULL segment");
  NS_ABORT_IF_FALSE(aId != 0, "invalid ID");

  Unprotect(mSegment);

  char* frontSentinel;
  char* data;
  char* backSentinel;
  GetSections(aSegment, &frontSentinel, &data, &backSentinel);

  // do a quick validity check to avoid weird-looking crashes in libc
  char check = *frontSentinel;
  (void)check;

  Header* header = reinterpret_cast<Header*>(frontSentinel);
  NS_ABORT_IF_FALSE(!strncmp(header->mMagic, sMagic, sizeof(sMagic)),
                      "invalid segment");
  mSize = header->mSize;

  size_t pageSize = SharedMemory::SystemPageSize();
  // transition into the "mapped" state by protecting the front and
  // back sentinels (which guard against buffer under/overflows)
  mSegment->Protect(frontSentinel, pageSize, RightsNone);
  mSegment->Protect(backSentinel, pageSize, RightsNone);

  // don't set these until we know they're valid
  mData = data;
  mId = aId;
}

void
Shmem::AssertInvariants() const
{
  NS_ABORT_IF_FALSE(mSegment, "NULL segment");
  NS_ABORT_IF_FALSE(mData, "NULL data pointer");
  NS_ABORT_IF_FALSE(mSize > 0, "invalid size");
  // if the segment isn't owned by the current process, these will
  // trigger SIGSEGV
  char checkMappingFront = *reinterpret_cast<char*>(mData);
  char checkMappingBack = *(reinterpret_cast<char*>(mData) + mSize - 1);
  checkMappingFront = checkMappingBack; // avoid "unused" warnings
}

void
Shmem::RevokeRights(IHadBetterBeIPDLCodeCallingThis_OtherwiseIAmADoodyhead)
{
  AssertInvariants();
  Protect(mSegment);
}

// static
Shmem::SharedMemory*
Shmem::Alloc(IHadBetterBeIPDLCodeCallingThis_OtherwiseIAmADoodyhead,
             size_t aNBytes,
             SharedMemoryType aType,
             bool aProtect)
{
  size_t pageSize = SharedMemory::SystemPageSize();
  SharedMemory* segment = nsnull;
  // |2*pageSize| is for the front and back sentinel
  size_t segmentSize = PageAlignedSize(aNBytes + 2*pageSize);

  if (aType == SharedMemory::TYPE_BASIC)
    segment = CreateSegment(segmentSize, SharedMemoryBasic::NULLHandle());
#ifdef MOZ_HAVE_SHAREDMEMORYSYSV
  else if (aType == SharedMemory::TYPE_SYSV)
    segment = CreateSegment(segmentSize, SharedMemorySysV::NULLHandle());
#endif
  else
    NS_RUNTIMEABORT("unknown shmem type");

  if (!segment)
    return 0;

  char *frontSentinel;
  char *data;
  char *backSentinel;
  GetSections(segment, &frontSentinel, &data, &backSentinel);

  // initialize the segment with Shmem-internal information
  Header* header = reinterpret_cast<Header*>(frontSentinel);
  memcpy(header->mMagic, sMagic, sizeof(sMagic));
  header->mSize = aNBytes;

  if (aProtect)
    Protect(segment);

  return segment;
}

// static
Shmem::SharedMemory*
Shmem::OpenExisting(IHadBetterBeIPDLCodeCallingThis_OtherwiseIAmADoodyhead,
                    const IPC::Message& aDescriptor,
                    id_t* aId,
                    bool aProtect)
{
  if (SHMEM_CREATED_MESSAGE_TYPE != aDescriptor.type())
    NS_RUNTIMEABORT("expected 'shmem created' message");

  void* iter = 0;
  SharedMemory::SharedMemoryType type;
  size_t size;
  if (!ShmemCreated::ReadInfo(&aDescriptor, &iter, aId, &size, &type))
    return 0;

  SharedMemory* segment = 0;
  size_t pageSize = SharedMemory::SystemPageSize();
  // |2*pageSize| is for the front and back sentinels
  size_t segmentSize = PageAlignedSize(size + 2*pageSize);

  if (SharedMemory::TYPE_BASIC == type) {
    SharedMemoryBasic::Handle handle;
    if (!ShmemCreated::ReadHandle(&aDescriptor, &iter, &handle))
      return 0;

    if (!SharedMemoryBasic::IsHandleValid(handle))
      NS_RUNTIMEABORT("trying to open invalid handle");
    segment = CreateSegment(segmentSize, handle);
  }
#ifdef MOZ_HAVE_SHAREDMEMORYSYSV
  else if (SharedMemory::TYPE_SYSV == type) {
    SharedMemorySysV::Handle handle;
    if (!ShmemCreated::ReadHandle(&aDescriptor, &iter, &handle))
      return 0;

    if (!SharedMemorySysV::IsHandleValid(handle))
      NS_RUNTIMEABORT("trying to open invalid handle");
    segment = CreateSegment(segmentSize, handle);
  }
#endif
  else {
    NS_RUNTIMEABORT("unknown shmem type");
  }

  if (!segment)
    return 0;

  if (aProtect)
    Protect(segment);

  return segment;
}

// static
void
Shmem::Dealloc(IHadBetterBeIPDLCodeCallingThis_OtherwiseIAmADoodyhead,
               SharedMemory* aSegment)
{
  if (!aSegment)
    return;

  size_t pageSize = SharedMemory::SystemPageSize();
  char *frontSentinel;
  char *data;
  char *backSentinel;
  GetSections(aSegment, &frontSentinel, &data, &backSentinel);

  aSegment->Protect(frontSentinel, pageSize, RightsWrite | RightsRead);
  Header* header = reinterpret_cast<Header*>(frontSentinel);
  memset(header->mMagic, 0, sizeof(sMagic));
  header->mSize = 0;

  DestroySegment(aSegment);
}


#else  // !defined(DEBUG)

// static
Shmem::SharedMemory*
Shmem::Alloc(IHadBetterBeIPDLCodeCallingThis_OtherwiseIAmADoodyhead,
             size_t aNBytes, 
             SharedMemoryType aType,
             bool /*unused*/)
{
  SharedMemory *segment = nsnull;

  if (aType == SharedMemory::TYPE_BASIC)
    segment = CreateSegment(PageAlignedSize(aNBytes + sizeof(size_t)),
                            SharedMemoryBasic::NULLHandle());
#ifdef MOZ_HAVE_SHAREDMEMORYSYSV
  else if (aType == SharedMemory::TYPE_SYSV)
    segment = CreateSegment(PageAlignedSize(aNBytes + sizeof(size_t)),
                            SharedMemorySysV::NULLHandle());
#endif
  else
    // Unhandled!!
    NS_ABORT();

  if (!segment)
    return 0;

  *PtrToSize(segment) = aNBytes;

  return segment;
}

// static
Shmem::SharedMemory*
Shmem::OpenExisting(IHadBetterBeIPDLCodeCallingThis_OtherwiseIAmADoodyhead,
                    const IPC::Message& aDescriptor,
                    id_t* aId,
                    bool /*unused*/)
{
  if (SHMEM_CREATED_MESSAGE_TYPE != aDescriptor.type())
    NS_RUNTIMEABORT("expected 'shmem created' message");

  SharedMemory::SharedMemoryType type;
  void* iter = 0;
  size_t size;
  if (!ShmemCreated::ReadInfo(&aDescriptor, &iter, aId, &size, &type))
    return 0;

  SharedMemory* segment = 0;
  size_t segmentSize = PageAlignedSize(size + sizeof(size_t));

  if (SharedMemory::TYPE_BASIC == type) {
    SharedMemoryBasic::Handle handle;
    if (!ShmemCreated::ReadHandle(&aDescriptor, &iter, &handle))
      return 0;

    if (!SharedMemoryBasic::IsHandleValid(handle))
      NS_RUNTIMEABORT("trying to open invalid handle");

    segment = CreateSegment(segmentSize, handle);
  }
#ifdef MOZ_HAVE_SHAREDMEMORYSYSV
  else if (SharedMemory::TYPE_SYSV == type) {
    SharedMemorySysV::Handle handle;
    if (!ShmemCreated::ReadHandle(&aDescriptor, &iter, &handle))
      return 0;

    if (!SharedMemorySysV::IsHandleValid(handle))
      NS_RUNTIMEABORT("trying to open invalid handle");
    segment = CreateSegment(segmentSize, handle);
  }
#endif
  else {
    NS_RUNTIMEABORT("unknown shmem type");
  }

  if (!segment)
    return 0;

  // this is the only validity check done OPT builds
  if (size != *PtrToSize(segment))
    NS_RUNTIMEABORT("Alloc() segment size disagrees with OpenExisting()'s");

  return segment;
}

// static
void
Shmem::Dealloc(IHadBetterBeIPDLCodeCallingThis_OtherwiseIAmADoodyhead,
               SharedMemory* aSegment)
{
  DestroySegment(aSegment);
}

#endif  // if defined(DEBUG)

int
Shmem::GetSysVID() const
{
#ifdef MOZ_HAVE_SHAREDMEMORYSYSV
  AssertInvariants();

  if (mSegment->Type() != SharedMemory::TYPE_SYSV)
    NS_RUNTIMEABORT("Can't call GetSysVID() on a non-SysV Shmem!");

  SharedMemorySysV* seg = static_cast<SharedMemorySysV*>(mSegment);
  return seg->GetHandle();
#else
  NS_RUNTIMEABORT("Can't call GetSysVID() with no support for SysV shared memory!");
  return -1;                    // not reached
#endif
}

IPC::Message*
Shmem::ShareTo(IHadBetterBeIPDLCodeCallingThis_OtherwiseIAmADoodyhead,
               base::ProcessHandle aProcess,
               int32 routingId)
{
  AssertInvariants();

  if (SharedMemory::TYPE_BASIC == mSegment->Type()) {
    SharedMemoryBasic* seg = static_cast<SharedMemoryBasic*>(mSegment);
    SharedMemoryBasic::Handle handle;
    if (!seg->ShareToProcess(aProcess, &handle))
      return 0;

    return new ShmemCreated(routingId, mId, mSize, handle);
  }
#ifdef MOZ_HAVE_SHAREDMEMORYSYSV
  else if (SharedMemory::TYPE_SYSV == mSegment->Type()) {
    SharedMemorySysV* seg = static_cast<SharedMemorySysV*>(mSegment);
    return new ShmemCreated(routingId, mId, mSize, seg->GetHandle());
  }
#endif
  else {
    NS_RUNTIMEABORT("unknown shmem type (here?!)");
  }

  return 0;
}

IPC::Message*
Shmem::UnshareFrom(IHadBetterBeIPDLCodeCallingThis_OtherwiseIAmADoodyhead,
                   base::ProcessHandle aProcess,
                   int32 routingId)
{
  AssertInvariants();
  return new ShmemDestroyed(routingId, mId);
}

} // namespace ipc
} // namespace mozilla
