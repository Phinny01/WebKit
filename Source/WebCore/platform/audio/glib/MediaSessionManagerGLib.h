/*
 *  Copyright (C) 2021 Igalia S.L
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
 */

#pragma once

#if USE(GLIB) && ENABLE(MEDIA_SESSION)

#include "MediaSessionIdentifier.h"
#include "NowPlayingManager.h"
#include "PlatformMediaSessionManager.h"
#include <wtf/TZoneMalloc.h>
#include <wtf/glib/GRefPtr.h>

namespace WebCore {

struct NowPlayingInfo;
class MediaSessionGLib;

class MediaSessionManagerGLib
    : public PlatformMediaSessionManager
    , private NowPlayingManagerClient {
    WTF_MAKE_TZONE_ALLOCATED(MediaSessionManagerGLib);
public:
    MediaSessionManagerGLib(GRefPtr<GDBusNodeInfo>&&);
    ~MediaSessionManagerGLib();

    void beginInterruption(PlatformMediaSession::InterruptionType) final;

    bool hasActiveNowPlayingSession() const final { return m_nowPlayingActive; }
    String lastUpdatedNowPlayingTitle() const final { return m_lastUpdatedNowPlayingTitle; }
    double lastUpdatedNowPlayingDuration() const final { return m_lastUpdatedNowPlayingDuration; }
    double lastUpdatedNowPlayingElapsedTime() const final { return m_lastUpdatedNowPlayingElapsedTime; }
    std::optional<MediaUniqueIdentifier> lastUpdatedNowPlayingInfoUniqueIdentifier() const final { return m_lastUpdatedNowPlayingInfoUniqueIdentifier; }
    bool registeredAsNowPlayingApplication() const final { return m_registeredAsNowPlayingApplication; }
    bool haveEverRegisteredAsNowPlayingApplication() const final { return m_haveEverRegisteredAsNowPlayingApplication; }

    void dispatch(PlatformMediaSession::RemoteControlCommandType, PlatformMediaSession::RemoteCommandArgument);

    const GRefPtr<GDBusNodeInfo>& mprisInterface() const { return m_mprisInterface; }
    void setPrimarySessionIfNeeded(PlatformMediaSessionInterface&);
    void unregisterAllOtherSessions(PlatformMediaSessionInterface&);
    WeakPtr<PlatformMediaSession> nowPlayingEligibleSession();

    void setDBusNotificationsEnabled(bool dbusNotificationsEnabled) { m_dbusNotificationsEnabled = dbusNotificationsEnabled; }
    bool areDBusNotificationsEnabled() const { return m_dbusNotificationsEnabled; }

protected:
    void scheduleSessionStatusUpdate() final;
    void updateNowPlayingInfo();

    void removeSession(PlatformMediaSessionInterface&) final;
    void addSession(PlatformMediaSessionInterface&) final;
    void setCurrentSession(PlatformMediaSessionInterface&) final;

    bool sessionWillBeginPlayback(PlatformMediaSessionInterface&) override;
    void sessionWillEndPlayback(PlatformMediaSessionInterface&, DelayCallingUpdateNowPlaying) override;
    void sessionStateChanged(PlatformMediaSessionInterface&) override;
    void sessionDidEndRemoteScrubbing(PlatformMediaSessionInterface&) final;
    void clientCharacteristicsChanged(PlatformMediaSessionInterface&, bool) final;
    void sessionCanProduceAudioChanged() final;

    virtual void providePresentingApplicationPIDIfNecessary() { }

    void addSupportedCommand(PlatformMediaSession::RemoteControlCommandType) final;
    void removeSupportedCommand(PlatformMediaSession::RemoteControlCommandType) final;
    RemoteCommandListener::RemoteCommandsSet supportedCommands() const final;

    void resetHaveEverRegisteredAsNowPlayingApplicationForTesting() final { m_haveEverRegisteredAsNowPlayingApplication = false; };

private:
#if !RELEASE_LOG_DISABLED
    ASCIILiteral logClassName() const override { return "MediaSessionManagerGLib"_s; }
#endif

    // NowPlayingManagerClient
    void didReceiveRemoteControlCommand(PlatformMediaSession::RemoteControlCommandType type, const PlatformMediaSession::RemoteCommandArgument& argument) final { processDidReceiveRemoteControlCommand(type, argument); }

    bool m_isSeeking { false };
    GRefPtr<GDBusNodeInfo> m_mprisInterface;

    bool m_nowPlayingActive { false };
    bool m_registeredAsNowPlayingApplication { false };
    bool m_haveEverRegisteredAsNowPlayingApplication { false };

    // For testing purposes only.
    String m_lastUpdatedNowPlayingTitle;
    double m_lastUpdatedNowPlayingDuration { NAN };
    double m_lastUpdatedNowPlayingElapsedTime { NAN };
    Markable<MediaUniqueIdentifier> m_lastUpdatedNowPlayingInfoUniqueIdentifier;

    const std::unique_ptr<NowPlayingManager> m_nowPlayingManager;
    HashMap<MediaSessionIdentifier, std::unique_ptr<MediaSessionGLib>> m_sessions;

    bool m_dbusNotificationsEnabled { true };
};

} // namespace WebCore

#endif // USE(GLIB) && ENABLE(MEDIA_SESSION)
