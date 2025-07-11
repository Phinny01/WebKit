/*
 * Copyright (C) 2013-2021 Apple Inc. All rights reserved.
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

#if ENABLE(DFG_JIT)

#include "BaselineJITCode.h"
#include "CallLinkInfo.h"
#include "CodeBlockJettisoningWatchpoint.h"
#include "ConcatKeyAtomStringCache.h"
#include "DFGAdaptiveInferredPropertyValueWatchpoint.h"
#include "DFGAdaptiveStructureWatchpoint.h"
#include "DFGCodeOriginPool.h"
#include "DFGJumpReplacement.h"
#include "DFGOSREntry.h"
#include "InlineCallFrameSet.h"
#include "JITMathIC.h"
#include "JSCast.h"
#include "PCToCodeOriginMap.h"
#include "ProfilerCompilation.h"
#include "RecordedStatuses.h"
#include "StructureStubInfo.h"
#include "YarrJIT.h"
#include <wtf/Bag.h>
#include <wtf/Noncopyable.h>
#include <wtf/text/StringSearch.h>

namespace JSC {

class CodeBlock;
class Identifier;
class TrackedReferences;

namespace DFG {

struct Node;
class Plan;

// CommonData holds the set of data that both DFG and FTL code blocks need to know
// about themselves.

struct WeakReferenceTransition {
    WeakReferenceTransition() { }
    
    WeakReferenceTransition(VM& vm, JSCell* owner, JSCell* codeOrigin, JSCell* from, JSCell* to)
        : m_from(vm, owner, from)
        , m_to(vm, owner, to)
    {
        if (!!codeOrigin)
            m_codeOrigin.set(vm, owner, codeOrigin);
    }
    
    WriteBarrier<JSCell> m_codeOrigin;
    WriteBarrier<JSCell> m_from;
    WriteBarrier<JSCell> m_to;
};
        
class CommonData : public MathICHolder {
    WTF_MAKE_NONCOPYABLE(CommonData);
public:
    CommonData(bool isUnlinked)
        : codeOrigins(CodeOriginPool::create())
        , m_isUnlinked(isUnlinked)
    { }
    ~CommonData();
    
    void shrinkToFit();
    
    bool invalidateLinkedCode(); // Returns true if we did invalidate, or false if the code block was already invalidated.
    bool hasInstalledVMTrapsBreakpoints() const { return m_isStillValid && m_hasVMTrapsBreakpointsInstalled; }
    void installVMTrapBreakpoints(CodeBlock* owner);

    bool isUnlinked() const { return m_isUnlinked; }
    bool isStillValid() const { return m_isStillValid; }

    CatchEntrypointData* catchOSREntryDataForBytecodeIndex(BytecodeIndex bytecodeIndex)
    {
        return tryBinarySearch<CatchEntrypointData, BytecodeIndex>(
            m_catchEntrypoints, m_catchEntrypoints.size(), bytecodeIndex,
            [] (const CatchEntrypointData* item) { return item->bytecodeIndex; });
    }

    void finalizeCatchEntrypoints(Vector<CatchEntrypointData>&&);

    unsigned requiredRegisterCountForExecutionAndExit() const
    {
        return std::max(frameRegisterCount, requiredRegisterCountForExit);
    }
    
    void validateReferences(const TrackedReferences&);

    static constexpr ptrdiff_t frameRegisterCountOffset() { return OBJECT_OFFSETOF(CommonData, frameRegisterCount); }
    
    void clearWatchpoints();

    RefPtr<InlineCallFrameSet> inlineCallFrames;
    Ref<CodeOriginPool> codeOrigins;
    
    FixedVector<Identifier> m_dfgIdentifiers;
    FixedVector<WeakReferenceTransition> m_transitions;
    FixedVector<WriteBarrier<JSCell>> m_weakReferences;
    FixedVector<StructureID> m_weakStructureReferences;
    FixedVector<CatchEntrypointData> m_catchEntrypoints;
    FixedVector<CodeBlockJettisoningWatchpoint> m_watchpoints;
    FixedVector<AdaptiveStructureWatchpoint> m_adaptiveStructureWatchpoints;
    FixedVector<AdaptiveInferredPropertyValueWatchpoint> m_adaptiveInferredPropertyValueWatchpoints;
    std::unique_ptr<PCToCodeOriginMap> m_pcToCodeOriginMap;
    std::unique_ptr<RecordedStatuses> recordedStatuses;
    FixedVector<JumpReplacement> m_jumpReplacements;
    FixedVector<std::unique_ptr<BoyerMooreHorspoolTable<uint8_t>>> m_stringSearchTable8;
    FixedVector<std::unique_ptr<ConcatKeyAtomStringCache>> m_concatKeyAtomStringCaches;
    Bag<StructureStubInfo> m_stubInfos;
    Bag<OptimizingCallLinkInfo> m_callLinkInfos;
    Bag<DirectCallLinkInfo> m_directCallLinkInfos;
    Yarr::YarrBoyerMooreData m_boyerMooreData;
    
    ScratchBuffer* catchOSREntryBuffer;
    RefPtr<Profiler::Compilation> compilation;
    
#if USE(JSVALUE32_64)
    Bag<double> doubleConstants;
#endif
    
    unsigned frameRegisterCount { std::numeric_limits<unsigned>::max() };
    unsigned requiredRegisterCountForExit { std::numeric_limits<unsigned>::max() };

private:
    bool m_isUnlinked { false };
    bool m_isStillValid { true };
    bool m_hasVMTrapsBreakpointsInstalled { false };
};

CodeBlock* codeBlockForVMTrapPC(void* pc);

} } // namespace JSC::DFG

#endif // ENABLE(DFG_JIT)
