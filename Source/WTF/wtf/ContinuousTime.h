/*
 * Copyright (C) 2025 Apple Inc. All rights reserved.
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

#include <wtf/ClockType.h>
#include <wtf/GenericTimeMixin.h>

namespace WTF {

class WallTime;
class PrintStream;

// The current time according to a continuous monotonic clock, which continues ticking while the
// system is asleep.
class ContinuousTime final : public GenericTimeMixin<ContinuousTime> {
public:
    static constexpr ClockType clockType = ClockType::Continuous;

    // This is the epoch. So, x.secondsSinceEpoch() should be the same as x - ContinuousTime().
    constexpr ContinuousTime() = default;

#if OS(DARWIN)
    WTF_EXPORT_PRIVATE static ContinuousTime fromMachContinuousTime(uint64_t);
    WTF_EXPORT_PRIVATE uint64_t toMachContinuousTime() const;
#endif

    WTF_EXPORT_PRIVATE static ContinuousTime now();

    WTF_EXPORT_PRIVATE WallTime approximateWallTime() const;
    WTF_EXPORT_PRIVATE MonotonicTime approximateMonotonicTime() const;

    WTF_EXPORT_PRIVATE void dump(PrintStream&) const;

private:
    friend class GenericTimeMixin<ContinuousTime>;
    constexpr ContinuousTime(double rawValue)
        : GenericTimeMixin<ContinuousTime>(rawValue)
    {
    }
};
static_assert(sizeof(ContinuousTime) == sizeof(double));

template<>
struct MarkableTraits<ContinuousTime> {
    static bool isEmptyValue(ContinuousTime time)
    {
        return time.isNaN();
    }

    static constexpr ContinuousTime emptyValue()
    {
        return ContinuousTime::nan();
    }
};

} // namespace WTF

using WTF::ContinuousTime;
