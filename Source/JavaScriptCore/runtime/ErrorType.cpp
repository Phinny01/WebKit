/*
 * Copyright (C) 2019 Apple Inc. All rights reserved.
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

#include "config.h"
#include "ErrorType.h"

#include <wtf/PrintStream.h>

WTF_ALLOW_UNSAFE_BUFFER_USAGE_BEGIN

namespace JSC {

ASCIILiteral errorTypeName(ErrorType errorType)
{
    return errorTypeName(static_cast<ErrorTypeWithExtension>(errorType));
}

ASCIILiteral errorTypeName(ErrorTypeWithExtension errorType)
{
    static const ASCIILiteral errorTypeNames[] = {
#define DECLARE_ERROR_TYPES_STRING(name) #name ""_s,
        JSC_ERROR_TYPES_WITH_EXTENSION(DECLARE_ERROR_TYPES_STRING)
#undef DECLARE_ERROR_TYPES_STRING
    };
    return errorTypeNames[static_cast<unsigned>(errorType)];
}

} // namespace JSC

namespace WTF {

void printInternal(PrintStream& out, JSC::ErrorType errorType)
{
    out.print(JSC::errorTypeName(errorType));
}

void printInternal(PrintStream& out, JSC::ErrorTypeWithExtension errorType)
{
    out.print(JSC::errorTypeName(errorType));
}

} // namespace WTF

WTF_ALLOW_UNSAFE_BUFFER_USAGE_END
