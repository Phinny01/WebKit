/*
 * Copyright (C) 2022-2025 Apple Inc. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#if ENABLE(GPU_PROCESS)

#include "RemoteGPUProxy.h"
#include "WebGPUIdentifier.h"
#include <WebCore/WebGPUIntegralTypes.h>
#include <WebCore/WebGPUPresentationContext.h>
#include <array>
#include <wtf/TZoneMalloc.h>

namespace WebCore {
class NativeImage;
}

namespace WebKit::WebGPU {

class ConvertToBackingContext;
class RemoteTextureProxy;

class RemotePresentationContextProxy final : public WebCore::WebGPU::PresentationContext {
    WTF_MAKE_TZONE_ALLOCATED(RemotePresentationContextProxy);
public:
    static Ref<RemotePresentationContextProxy> create(RemoteGPUProxy& parent, ConvertToBackingContext& convertToBackingContext, WebGPUIdentifier identifier)
    {
        return adoptRef(*new RemotePresentationContextProxy(parent, convertToBackingContext, identifier));
    }

    virtual ~RemotePresentationContextProxy();

    RemoteGPUProxy& parent() const { return m_parent; }
    RemoteGPUProxy& root() { return m_parent->root(); }
    Ref<RemoteGPUProxy> protectedRoot() { return m_parent->root(); }

    void present(uint32_t frameIndex, bool = false) final;

private:
    friend class DowncastConvertToBackingContext;

    RemotePresentationContextProxy(RemoteGPUProxy&, ConvertToBackingContext&, WebGPUIdentifier);

    RemotePresentationContextProxy(const RemotePresentationContextProxy&) = delete;
    RemotePresentationContextProxy(RemotePresentationContextProxy&&) = delete;
    RemotePresentationContextProxy& operator=(const RemotePresentationContextProxy&) = delete;
    RemotePresentationContextProxy& operator=(RemotePresentationContextProxy&&) = delete;

    WebGPUIdentifier backing() const { return m_backing; }

    RefPtr<WebCore::NativeImage> getMetalTextureAsNativeImage(uint32_t, bool& isIOSurfaceSupportedFormat) final;

    template<typename T>
    WARN_UNUSED_RETURN IPC::Error send(T&& message)
    {
        return root().protectedStreamClientConnection()->send(WTFMove(message), backing());
    }

    bool configure(const WebCore::WebGPU::CanvasConfiguration&) final;
    void unconfigure() final;

    RefPtr<WebCore::WebGPU::Texture> getCurrentTexture(uint32_t) final;

    WebGPUIdentifier m_backing;
    const Ref<ConvertToBackingContext> m_convertToBackingContext;
    const Ref<RemoteGPUProxy> m_parent;
    static constexpr size_t textureCount = 3;
    std::array<RefPtr<RemoteTextureProxy>, textureCount> m_currentTexture;
};

} // namespace WebKit::WebGPU

#endif // ENABLE(GPU_PROCESS)
