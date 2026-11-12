#pragma once

#include "engine/channels/enginechannel.h"
#include "engine/syncable.h"
#include "engine/enginebuffer.h"

#include <memory>

QT_FORWARD_DECLARE_CLASS(QUrl)
QT_FORWARD_DECLARE_CLASS(QTimer)

namespace mixxx {

// Forward declarations
class EngineMidiDeck;
using EngineMidiDeckPointer = std::shared_ptr<EngineMidiDeck>;

// EngineMidiDeck — bridges Mixxx deck transport controls with MIDI event scheduling
// Implements EngineChannel (for Mixxx deck integration) and Syncable (for BPM sync)
class EngineMidiDeck final : public EngineChannel, public Syncable {
    Q_OBJECT

  public:
    // Deck group identifier for Mixxx controls
    static const QString kGroup;

    explicit EngineMidiDeck(
            const ChannelHandleAndGroup& handleGroup,
            EffectsManager* pEffectsManager,
            bool isPrimaryDeck);

    ~EngineMidiDeck() override;

    // EngineChannel overrides
    void process(CSAMPLE* pInOut,
            const std::size_t bufferSize) override;

    void collectFeatures(GroupFeatureState* pGroupFeatures) const override;

    // EngineChannel lifecycle
    ActiveState updateActiveState() override;
    bool isActive() override;

    // Syncable interface
    double getRateRatio() const override;
    void setRateRatio(double rateRatio) override;
    double getSyncBpm() const override;
    void setSyncBpm(double syncBpm) override;
    double getCurrentBpm() const override;
    void setCurrentBpm(double bpm) override;

    // Transport controls (MIDI-specific)
    void play();
    void pause();
    void stop();
    void cue();

    // Position controls
    double getPosition() const override;
    void setPosition(double positionSeconds) override;
    double getDuration() const;

    // Beatmatching controls
    void beatJumpForward(int beats) override;
    void beatJumpBackward(int beats) override;
    double getBeatJumpPosition() const;

    // MIDI-specific features
    int getTicksPerQuarterNote() const;
    double getTrackDurationSeconds() const;
    void setLoopStart(double seconds);
    void setLoopEnd(double seconds);
    bool isLoopEnabled() const;

    // Source access
    const QUrl& getSourceUrl() const;
    void loadSource(const QUrl& url);

  private:
    // Calculate tick rate based on BPM ratio
    void updateTickRate();

    // Timer callback for MIDI event scheduling
    void onTimerTick();

    // Track BPM for beat calculation
    double m_dBpm;
    double m_dRateRatio;
    double m_dSyncBpm;
    double m_dPositionSeconds;
    double m_dDurationSeconds;
    int m_nTicksPerQuarterNote;
    int m_nCurrentTick;

    // Sync state
    bool m_bSyncEnabled;
    ActiveState m_activeState;

    // MIDI state
    bool m_bPlaying;
    bool m_bLoopEnabled;
    double m_dLoopStart;
    double m_dLoopEnd;

    // Beat jump state
    bool m_bBeatJumping;
    double m_dBeatJumpTarget;

    // Source URL
    QUrl m_url;

    // Timer for event scheduling
    QTimer* m_pTimer;

    // Effects manager
    EffectsManager* m_pEffectsManager;
};

} // namespace mixxx
