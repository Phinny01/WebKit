/*
 * Copyright (C) 2016-2019 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
 */

#pragma once

#include "Environment.h"
#include "FailureAction.h"
#include "Mutex.h"
#include "StaticPerProcess.h"
#include <mutex>
#include <unordered_map>

#if BENABLE(MALLOC_HEAP_BREAKDOWN) || BOS(DARWIN)
#include <malloc/malloc.h>
#endif

namespace bmalloc {

class SystemHeap : private StaticPerProcess<SystemHeap> {
public:
    SystemHeap(const LockHolder&);

    void* malloc(size_t, FailureAction);
    void* memalign(size_t alignment, size_t, FailureAction);
    void* realloc(void*, size_t, FailureAction);
    void free(void*);

    void* memalignLarge(size_t alignment, size_t);
    void freeLarge(void* base);

#if BENABLE(MALLOC_SIZE)
    size_t mallocSize(const void* object) { return malloc_size(object); }
#endif

#if BENABLE(MALLOC_GOOD_SIZE)
    size_t mallocGoodSize(size_t size) { return malloc_good_size(size); }
#endif

    void scavenge();
    void dump();

    static SystemHeap* tryGet();
    static SystemHeap* getExisting();

#if BOS(DARWIN)
    malloc_zone_t* zone() const { return m_zone; };
#endif

private:
    static SystemHeap* tryGetSlow();

#if BENABLE(MALLOC_HEAP_BREAKDOWN) || BOS(DARWIN)
    malloc_zone_t* m_zone;
#endif

    // This is the system heap. We can use whatever data structures we like. It doesn't matter.
    size_t m_pageSize { 0 };
    std::unordered_map<void*, size_t> m_sizeMap;
};
BALLOW_DEPRECATED_DECLARATIONS_BEGIN
DECLARE_STATIC_PER_PROCESS_STORAGE(SystemHeap);
BALLOW_DEPRECATED_DECLARATIONS_END

extern BEXPORT SystemHeap* systemHeapCache;

BINLINE SystemHeap* systemHeapDisabled()
{
    return reinterpret_cast<SystemHeap*>(static_cast<uintptr_t>(1));
}

BINLINE SystemHeap* SystemHeap::tryGet()
{
    SystemHeap* result = systemHeapCache;
    if (result == systemHeapDisabled())
        return nullptr;
    if (result)
        return result;
    return tryGetSlow();
}

BINLINE SystemHeap* SystemHeap::getExisting()
{
    SystemHeap* result = tryGet();
    RELEASE_BASSERT(result);
    return result;
}

} // namespace bmalloc
