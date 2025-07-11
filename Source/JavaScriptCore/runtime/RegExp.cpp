/*
 *  Copyright (C) 1999-2001, 2004 Harri Porten (porten@kde.org)
 *  Copyright (c) 2007-2021 Apple Inc. All rights reserved.
 *  Copyright (C) 2009 Torch Mobile, Inc.
 *  Copyright (C) 2010 Peter Varga (pvarga@inf.u-szeged.hu), University of Szeged
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#include "config.h"
#include "RegExp.h"

#include "Lexer.h"
#include "RegExpCache.h"
#include "RegExpInlines.h"
#include "YarrJIT.h"
#include <wtf/Assertions.h>
#include <wtf/DataLog.h>
#include <wtf/text/MakeString.h>

namespace JSC {

const ClassInfo RegExp::s_info = { "RegExp"_s, nullptr, nullptr, nullptr, CREATE_METHOD_TABLE(RegExp) };

#if REGEXP_FUNC_TEST_DATA_GEN
const char* const RegExpFunctionalTestCollector::s_fileName = "/tmp/RegExpTestsData";
RegExpFunctionalTestCollector* RegExpFunctionalTestCollector::s_instance = 0;

RegExpFunctionalTestCollector* RegExpFunctionalTestCollector::get()
{
    if (!s_instance)
        s_instance = new RegExpFunctionalTestCollector();

    return s_instance;
}

void RegExpFunctionalTestCollector::outputOneTest(RegExp* regExp, StringView s, int startOffset, int* ovector, int result)
{
    if ((!m_lastRegExp) || (m_lastRegExp != regExp)) {
        m_lastRegExp = regExp;
        fputc('/', m_file);
        outputEscapedString(regExp->pattern(), true);
        fputc('/', m_file);
        fprintf(m_file, "%s\n", Yarr::flagsString(regExp->flags()).data());
    }

    fprintf(m_file, " \"");
    outputEscapedString(s);
    fprintf(m_file, "\", %d, %d, (", startOffset, result);
    for (unsigned i = 0; i <= regExp->numSubpatterns(); i++) {
        int subpatternBegin = ovector[i * 2];
        int subpatternEnd = ovector[i * 2 + 1];
        if (subpatternBegin == -1)
            subpatternEnd = -1;
        fprintf(m_file, "%d, %d", subpatternBegin, subpatternEnd);
        if (i < regExp->numSubpatterns())
            fputs(", ", m_file);
    }

    fprintf(m_file, ")\n");
    fflush(m_file);
}

RegExpFunctionalTestCollector::RegExpFunctionalTestCollector()
{
    m_file = fopen(s_fileName, "r+");
    if  (!m_file)
        m_file = fopen(s_fileName, "w+");

    fseek(m_file, 0L, SEEK_END);
}

RegExpFunctionalTestCollector::~RegExpFunctionalTestCollector()
{
    fclose(m_file);
    s_instance = 0;
}

void RegExpFunctionalTestCollector::outputEscapedString(StringView s, bool escapeSlash)
{
    int len = s.length();
    
    for (int i = 0; i < len; ++i) {
        char16_t c = s[i];

        switch (c) {
        case '\0':
            fputs("\\0", m_file);
            break;
        case '\a':
            fputs("\\a", m_file);
            break;
        case '\b':
            fputs("\\b", m_file);
            break;
        case '\f':
            fputs("\\f", m_file);
            break;
        case '\n':
            fputs("\\n", m_file);
            break;
        case '\r':
            fputs("\\r", m_file);
            break;
        case '\t':
            fputs("\\t", m_file);
            break;
        case '\v':
            fputs("\\v", m_file);
            break;
        case '/':
            if (escapeSlash)
                fputs("\\/", m_file);
            else
                fputs("/", m_file);
            break;
        case '\"':
            fputs("\\\"", m_file);
            break;
        case '\\':
            fputs("\\\\", m_file);
            break;
        case '\?':
            fputs("\?", m_file);
            break;
        default:
            if (c > 0x7f)
                fprintf(m_file, "\\u%04x", c);
            else 
                fputc(c, m_file);
            break;
        }
    }
}
#endif

RegExp::RegExp(VM& vm, const String& patternString, OptionSet<Yarr::Flags> flags)
    : JSCell(vm, vm.regExpStructure.get())
    , m_patternString(patternString)
    , m_flags(flags)
{
    ASSERT(m_flags != Yarr::Flags::DeletedValue);
}

void RegExp::finishCreation(VM& vm)
{
    Base::finishCreation(vm);
    Yarr::YarrPattern pattern(m_patternString, m_flags, m_constructionErrorCode);
    if (!isValid()) {
        m_state = ParseError;
        return;
    }

    m_atom = WTFMove(pattern.m_atom);
    m_specificPattern = pattern.m_specificPattern;

    m_numSubpatterns = pattern.m_numSubpatterns;
    if (!pattern.m_captureGroupNames.isEmpty() || !pattern.m_namedGroupToParenIndices.isEmpty()) {
        m_rareData = makeUnique<RareData>();
        m_rareData->m_numDuplicateNamedCaptureGroups = pattern.m_numDuplicateNamedCaptureGroups;
        m_rareData->m_captureGroupNames.swap(pattern.m_captureGroupNames);
        m_rareData->m_namedGroupToParenIndices.swap(pattern.m_namedGroupToParenIndices);
    }
}

void RegExp::destroy(JSCell* cell)
{
    RegExp* thisObject = static_cast<RegExp*>(cell);
#if REGEXP_FUNC_TEST_DATA_GEN
    RegExpFunctionalTestCollector::get()->clearRegExp(this);
#endif
    thisObject->RegExp::~RegExp();
}

size_t RegExp::estimatedSize(JSCell* cell, VM& vm)
{
    RegExp* thisObject = static_cast<RegExp*>(cell);
    size_t regexDataSize = thisObject->m_regExpBytecode ? thisObject->m_regExpBytecode->estimatedSizeInBytes() : 0;
#if ENABLE(YARR_JIT)
    if (auto* jitCode = thisObject->m_regExpJITCode.get())
        regexDataSize += jitCode->size();
#endif
    return Base::estimatedSize(cell, vm) + regexDataSize;
}

RegExp* RegExp::createWithoutCaching(VM& vm, const String& patternString, OptionSet<Yarr::Flags> flags)
{
    RegExp* regExp = new (NotNull, allocateCell<RegExp>(vm)) RegExp(vm, patternString, flags);
    regExp->finishCreation(vm);
    return regExp;
}

RegExp* RegExp::create(VM& vm, const String& patternString, OptionSet<Yarr::Flags> flags)
{
    return vm.regExpCache()->lookupOrCreate(vm, patternString, flags);
}


static std::unique_ptr<Yarr::BytecodePattern> byteCodeCompilePattern(VM* vm, Yarr::YarrPattern& pattern, Yarr::ErrorCode& errorCode)
{
    return Yarr::byteCompile(pattern, &vm->m_regExpAllocator, errorCode, &vm->m_regExpAllocatorLock);
}

void RegExp::byteCodeCompileIfNecessary(VM* vm)
{
    if (m_regExpBytecode)
        return;

    Yarr::YarrPattern pattern(m_patternString, m_flags, m_constructionErrorCode);
    if (hasError(m_constructionErrorCode)) {
        m_state = ParseError;
        return;
    }
    ASSERT(m_numSubpatterns == pattern.m_numSubpatterns);

    m_atom = WTFMove(pattern.m_atom);
    m_specificPattern = pattern.m_specificPattern;

    m_regExpBytecode = byteCodeCompilePattern(vm, pattern, m_constructionErrorCode);
    if (!m_regExpBytecode) {
        m_state = ParseError;
        return;
    }
}

void RegExp::compile(VM* vm, Yarr::CharSize charSize, std::optional<StringView> sampleString)
{
    Locker locker { cellLock() };
    
    Yarr::YarrPattern pattern(m_patternString, m_flags, m_constructionErrorCode);
    if (hasError(m_constructionErrorCode)) {
        m_state = ParseError;
        return;
    }
    ASSERT(m_numSubpatterns == pattern.m_numSubpatterns);

    m_atom = WTFMove(pattern.m_atom);
    m_specificPattern = pattern.m_specificPattern;

    if (!hasCode()) {
        ASSERT(m_state == NotCompiled);
        vm->regExpCache()->addToStrongCache(this);
        m_state = ByteCode;
    }

#if ENABLE(YARR_JIT)
    if (!pattern.containsUnsignedLengthPattern() && Options::useRegExpJIT()
#if !ENABLE(YARR_JIT_BACKREFERENCES)
        && !pattern.m_containsBackreferences
#endif
        && !pattern.m_containsLookbehinds
        ) {
        auto& jitCode = ensureRegExpJITCode();
        Yarr::jitCompile(pattern, m_patternString, charSize, sampleString, vm, jitCode, Yarr::JITCompileMode::IncludeSubpatterns);
        if (!jitCode.failureReason()) {
            m_state = JITCode;
            return;
        }
    }
#else
    UNUSED_PARAM(charSize);
    UNUSED_PARAM(sampleString);
#endif

    dataLogLnIf(Options::dumpCompiledRegExpPatterns(), "Can't JIT this regular expression: \"/", m_patternString, "/\"");

    m_state = ByteCode;
    m_regExpBytecode = byteCodeCompilePattern(vm, pattern, m_constructionErrorCode);
    if (!m_regExpBytecode) {
        m_state = ParseError;
        return;
    }
}

int RegExp::match(JSGlobalObject* globalObject, StringView s, unsigned startOffset, Vector<int>& ovector)
{
    return matchInline(globalObject, globalObject->vm(), s, startOffset, ovector);
}

bool RegExp::matchConcurrently(
    VM& vm, StringView s, unsigned startOffset, int& position, Vector<int>& ovector)
{
    Locker locker { cellLock() };

    if (!hasCodeFor(s.is8Bit() ? Yarr::CharSize::Char8 : Yarr::CharSize::Char16))
        return false;

    position = matchInline<Vector<int>&, Yarr::MatchFrom::CompilerThread>(nullptr, vm, s, startOffset, ovector);
    if (m_state == ParseError)
        return false;
    return true;
}

void RegExp::compileMatchOnly(VM* vm, Yarr::CharSize charSize, std::optional<StringView> sampleString)
{
    Locker locker { cellLock() };
    
    Yarr::YarrPattern pattern(m_patternString, m_flags, m_constructionErrorCode);
    if (hasError(m_constructionErrorCode)) {
        m_state = ParseError;
        return;
    }
    ASSERT(m_numSubpatterns == pattern.m_numSubpatterns);

    m_atom = WTFMove(pattern.m_atom);
    m_specificPattern = pattern.m_specificPattern;

    if (!hasCode()) {
        ASSERT(m_state == NotCompiled);
        vm->regExpCache()->addToStrongCache(this);
        m_state = ByteCode;
    }

#if ENABLE(YARR_JIT)
    if (!pattern.containsUnsignedLengthPattern() && Options::useRegExpJIT()
#if !ENABLE(YARR_JIT_BACKREFERENCES)
        && !pattern.m_containsBackreferences
#endif
        && !pattern.m_containsLookbehinds
        ) {
        auto& jitCode = ensureRegExpJITCode();
        Yarr::jitCompile(pattern, m_patternString, charSize, sampleString, vm, jitCode, Yarr::JITCompileMode::MatchOnly);
        if (!jitCode.failureReason()) {
            m_state = JITCode;
            return;
        }
    }
#else
    UNUSED_PARAM(charSize);
    UNUSED_PARAM(sampleString);
#endif

    dataLogLnIf(Options::dumpCompiledRegExpPatterns(), "Can't JIT this regular expression: \"/", m_patternString, "/\"");

    m_state = ByteCode;
    m_regExpBytecode = byteCodeCompilePattern(vm, pattern, m_constructionErrorCode);
    if (!m_regExpBytecode) {
        m_state = ParseError;
        return;
    }
}

MatchResult RegExp::match(JSGlobalObject* globalObject, StringView s, unsigned startOffset)
{
    return matchInline(globalObject, globalObject->vm(), s, startOffset);
}

bool RegExp::matchConcurrently(VM& vm, StringView s, unsigned startOffset, MatchResult& result)
{
    Locker locker { cellLock() };

    if (!hasMatchOnlyCodeFor(s.is8Bit() ? Yarr::CharSize::Char8 : Yarr::CharSize::Char16))
        return false;

    result = matchInline<Yarr::MatchFrom::CompilerThread>(nullptr, vm, s, startOffset);
    return true;
}

void RegExp::deleteCode()
{
    Locker locker { cellLock() };
    
    if (!hasCode())
        return;
    m_state = NotCompiled;
    m_atom = String();
    m_specificPattern = Yarr::SpecificPattern::None;
#if ENABLE(YARR_JIT)
    if (m_regExpJITCode)
        m_regExpJITCode->clear(locker);
#endif
    m_regExpBytecode = nullptr;
}

#if ENABLE(YARR_JIT_DEBUG)
void RegExp::matchCompareWithInterpreter(StringView s, int startOffset, int* offsetVector, int jitResult)
{
    int offsetVectorSize = (m_numSubpatterns + 1) * 2;
    Vector<int> interpreterOvector(offsetVectorSize);
    int* interpreterOffsetVector = interpreterOvector.data();
    int interpreterResult = 0;
    int differences = 0;

    // Initialize interpreterOffsetVector with the return value (index 0) and the 
    // first subpattern start indices (even index values) set to -1.
    // No need to init the subpattern end indices.
    for (unsigned j = 0, i = 0; i < m_numSubpatterns + 1; j += 2, i++)
        interpreterOffsetVector[j] = -1;

    interpreterResult = Yarr::interpret(m_regExpBytecode.get(), s, startOffset, reinterpret_cast<unsigned*>(interpreterOffsetVector));

    if (jitResult != interpreterResult)
        differences++;

    for (unsigned j = 2, i = 0; i < m_numSubpatterns; j +=2, i++)
        if ((offsetVector[j] != interpreterOffsetVector[j])
            || ((offsetVector[j] >= 0) && (offsetVector[j+1] != interpreterOffsetVector[j+1])))
            differences++;

    if (differences) {
        dataLog("RegExp Discrepency for ", toSourceString(), "\n    string input ");
        unsigned segmentLen = s.length() - static_cast<unsigned>(startOffset);

        dataLogF((segmentLen < 150) ? "\"%s\"\n" : "\"%148s...\"\n", s.utf8().data() + startOffset);

        if (jitResult != interpreterResult) {
            dataLogF("    JIT result = %d, interpreted result = %d\n", jitResult, interpreterResult);
            differences--;
        } else {
            dataLogF("    Correct result = %d\n", jitResult);
        }

        if (differences) {
            for (unsigned j = 2, i = 0; i < m_numSubpatterns; j +=2, i++) {
                if (offsetVector[j] != interpreterOffsetVector[j])
                    dataLogF("    JIT offset[%d] = %d, interpreted offset[%d] = %d\n", j, offsetVector[j], j, interpreterOffsetVector[j]);
                if ((offsetVector[j] >= 0) && (offsetVector[j+1] != interpreterOffsetVector[j+1]))
                    dataLogF("    JIT offset[%d] = %d, interpreted offset[%d] = %d\n", j+1, offsetVector[j+1], j+1, interpreterOffsetVector[j+1]);
            }
        }
    }
}
#endif

#if ENABLE(REGEXP_TRACING)
void RegExp::printTraceHeader()
{
    dataLogF("\nRegExp Tracing\n");
    dataLogF("Regular Expression");
    for (unsigned i = 0; i < SameLineFormatedRegExpnWidth - 16; ++i)
        dataLogF(" ");
    dataLogF("    8 Bit       16 Bit     match()    Matches    Average\n");
    dataLogF(" <Match only / Match>");
    for (unsigned i = 0; i < RegExp::SameLineFormatedRegExpnWidth - 21; ++i)
        dataLogF(" ");
    dataLogF("    JIT Addr     JIT Addr     calls      found   String len\n");
    for (unsigned i = 0; i < RegExp::SameLineFormatedRegExpnWidth; ++i)
        dataLogF("-");

    dataLogF("+------------+------------+----------+----------+-----------\n");
}

void RegExp::printTraceData()
{
    char formattedRegExp[SameLineFormatedRegExpnWidth + 1];
    char rawPatternBuffer[SameLineFormatedRegExpnWidth + 1];
    String rawPattern;

    memset(formattedRegExp, ' ', SameLineFormatedRegExpnWidth);
    formattedRegExp[SameLineFormatedRegExpnWidth] = '\0';

    auto patternCStr = pattern().utf8(); // Hold a reference so it doesn't get destroyed.
    auto patternStr = patternCStr.data();
    auto patternLength = pattern().length();

    auto appendRawPatternBuffer = [&] (size_t& index) {
        if (!index)
            return;

        rawPatternBuffer[index] = '\0';

        rawPattern = makeString(rawPattern, rawPatternBuffer);

        index = 0;
    };

    // Escape literal TAB characters.
    size_t dstIdx = 0;
    for (size_t srcIdx = 0; srcIdx < patternLength; ++srcIdx) {
        auto c = patternStr[srcIdx];
        if (c == '\t') {
            rawPatternBuffer[dstIdx++] = '\\';
            if (dstIdx >= SameLineFormatedRegExpnWidth)
                appendRawPatternBuffer(dstIdx);
            c = 't';
        }

        rawPatternBuffer[dstIdx++] = c;
        if (dstIdx >= SameLineFormatedRegExpnWidth)
            appendRawPatternBuffer(dstIdx);
    }

    appendRawPatternBuffer(dstIdx);

    if (rawPattern.length() + strlen(Yarr::flagsString(flags()).data()) + 2 <= SameLineFormatedRegExpnWidth) {
        String result = makeString('/', rawPattern, '/', Yarr::flagsString(flags()).data());
        memcpy(formattedRegExp, result.utf8().data(), result.length());
        formattedRegExp[result.length()] = '\0';
    } else
        dataLogF("/%s/%s\n", rawPattern.utf8().data(), Yarr::flagsString(flags()).data());

    constexpr int addrWidth = 12;
#if ENABLE(YARR_JIT)
    constexpr char hexDigits[] = "0123456789abcdef";
    constexpr char fallback[] = "fallback  ";
    constexpr char dashes[] = "----    ";

    String jit8BitMatchOnlyAddr;
    String jit16BitMatchOnlyAddr;
    String jit8BitMatchAddr;
    String jit16BitMatchAddr;

    auto formatAddress = [&] (void* addr) {
        constexpr int jitAddrSignificantWidth = addrWidth - 2;
        uintptr_t addrAsUint = reinterpret_cast<uintptr_t>(addr);

        String formatResult;
        for (int digit = jitAddrSignificantWidth; digit; --digit) {
            auto nibble = (addrAsUint >> ((digit - 1) * 4)) & 0xf;
            if (!formatResult.length()) {
                if (!nibble && digit > 8)
                    continue;

                formatResult = makeString("0x");
            }
            formatResult = makeString(formatResult, hexDigits[nibble]);
        }

        return formatResult;
    };

    switch (m_state) {
    case ParseError:
    case NotCompiled:
        break;
    case ByteCode:
        jit8BitMatchOnlyAddr = makeString(fallback);
        jit16BitMatchOnlyAddr = makeString(dashes);
        jit8BitMatchAddr = makeString(fallback);
        jit16BitMatchAddr = makeString(dashes);
        break;
    case JITCode: {
        Yarr::YarrCodeBlock& codeBlock = *m_regExpJITCode.get();
        jit8BitMatchOnlyAddr = formatAddress(codeBlock.get8BitMatchOnlyAddr());
        jit16BitMatchOnlyAddr = formatAddress(codeBlock.get16BitMatchOnlyAddr());
        jit8BitMatchAddr = formatAddress(codeBlock.get8BitMatchAddr());
        jit16BitMatchAddr = formatAddress(codeBlock.get16BitMatchAddr());
        break;
    }
    }
#else
    constexpr char jitOff[] = "JIT Off";
    String jit8BitMatchOnlyAddr = makeString(jitOff);
    String jit16BitMatchOnlyAddr;
    String jit8BitMatchAddr = makeString(jitOff);
    String jit16BitMatchAddr;
#endif
    unsigned averageMatchOnlyStringLen = (unsigned)(m_rtMatchOnlyTotalSubjectStringLen / m_rtMatchOnlyCallCount);
    unsigned averageMatchStringLen = (unsigned)(m_rtMatchTotalSubjectStringLen / m_rtMatchCallCount);

    dataLogF("%-*.*s %*.*s %*.*s %10d %10d %10u\n", SameLineFormatedRegExpnWidth, SameLineFormatedRegExpnWidth, formattedRegExp, addrWidth, addrWidth, jit8BitMatchOnlyAddr.utf8().data(), addrWidth, addrWidth, jit16BitMatchOnlyAddr.utf8().data(), m_rtMatchOnlyCallCount, m_rtMatchOnlyFoundCount, averageMatchOnlyStringLen);
    for (unsigned i = 0; i < SameLineFormatedRegExpnWidth; ++i)
        dataLog(" ");
    dataLogF(" %*.*s %*.*s %10d %10d %10u\n", addrWidth, addrWidth, jit8BitMatchAddr.utf8().data(), addrWidth, addrWidth, jit16BitMatchAddr.utf8().data(), m_rtMatchCallCount, m_rtMatchFoundCount, averageMatchStringLen);
}
#endif

void RegExp::dumpToStream(const JSCell* cell, PrintStream& out)
{
    // This function can be called concurrently. So we must not ref m_pattern.
    auto* regExp = jsCast<const RegExp*>(cell);
    out.print(toCString("/", regExp->pattern().impl(), "/", Yarr::flagsString(regExp->flags()).data()));
}

template <typename CharacterType>
static inline void appendLineTerminatorEscape(StringBuilder&, CharacterType);

template <>
inline void appendLineTerminatorEscape<LChar>(StringBuilder& builder, LChar lineTerminator)
{
    if (lineTerminator == '\n')
        builder.append('n');
    else
        builder.append('r');
}

template <>
inline void appendLineTerminatorEscape<char16_t>(StringBuilder& builder, char16_t lineTerminator)
{
    if (lineTerminator == '\n')
        builder.append('n');
    else if (lineTerminator == '\r')
        builder.append('r');
    else if (lineTerminator == 0x2028)
        builder.append("u2028"_s);
    else
        builder.append("u2029"_s);
}

template <typename CharacterType>
static inline String escapePattern(const String& pattern, std::span<const CharacterType> characters)
{
    bool previousCharacterWasBackslash = false;
    bool inBrackets = false;
    bool shouldEscape = false;

    // 15.10.6.4 specifies that RegExp.prototype.toString must return '/' + source + '/',
    // and also states that the result must be a valid RegularExpressionLiteral. '//' is
    // not a valid RegularExpressionLiteral (since it is a single line comment), and hence
    // source cannot ever validly be "". If the source is empty, return a different Pattern
    // that would match the same thing.
    if (characters.empty())
        return "(?:)"_s;

    // early return for strings that don't contain a forwards slash and LineTerminator
    for (auto ch : characters) {
        if (!previousCharacterWasBackslash) {
            if (inBrackets) {
                if (ch == ']')
                    inBrackets = false;
            } else {
                if (ch == '/') {
                    shouldEscape = true;
                    break;
                }
                if (ch == '[')
                    inBrackets = true;
            }
        }

        if (Lexer<CharacterType>::isLineTerminator(ch)) {
            shouldEscape = true;
            break;
        }

        if (previousCharacterWasBackslash)
            previousCharacterWasBackslash = false;
        else
            previousCharacterWasBackslash = ch == '\\';
    }

    if (!shouldEscape)
        return pattern;

    previousCharacterWasBackslash = false;
    inBrackets = false;
    StringBuilder result;
    for (auto ch : characters) {
        if (!previousCharacterWasBackslash) {
            if (inBrackets) {
                if (ch == ']')
                    inBrackets = false;
            } else {
                if (ch == '/')
                    result.append('\\');
                else if (ch == '[')
                    inBrackets = true;
            }
        }

        // escape LineTerminator
        if (Lexer<CharacterType>::isLineTerminator(ch)) {
            if (!previousCharacterWasBackslash)
                result.append('\\');

            appendLineTerminatorEscape<CharacterType>(result, ch);
        } else
            result.append(ch);

        if (previousCharacterWasBackslash)
            previousCharacterWasBackslash = false;
        else
            previousCharacterWasBackslash = ch == '\\';
    }

    return result.toString();
}

String RegExp::escapedPattern() const
{
    if (m_patternString.is8Bit())
        return escapePattern(m_patternString, m_patternString.span8());
    return escapePattern(m_patternString, m_patternString.span16());
}

String RegExp::toSourceString() const
{
    return makeString('/', escapedPattern(), '/', unsafeSpan(Yarr::flagsString(flags()).data()));
}

} // namespace JSC
