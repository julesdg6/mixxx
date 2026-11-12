#include "engine/channels/enginemidideck.h"

#include "control/control_lookup.h"
#include "util/logger.h"
#include "util/time.h"

#include <QTimer>

namespace mixxx {

namespace {

const Logger kLogger("EngineMidiDeck");

} // anonymous namespace

//static
const QString EngineMidiDeck::kGroup = "MidiDeck";

// =============================================================================
// Constructor
// =============================================================================

EngineMidiDeck::EngineMidiDeck(
        const ChannelHandleAndGroup& handleGroup,
        EffectsManager* pEffectsManager,
        bool isPrimaryDeck)
        : EngineChannel(handleGroup,
                        EngineChannel::LEFT,
                        pEffectsManager,
                        false, // isTalkoverChannel
                        isPrimaryDeck),
          m_dBpm(120.0),
          m_dRateRatio(1.0),
          m_dSyncBpm(120.0),
          m_dPositionSeconds(0.0),
          m_dDurationSeconds(0.0),
          m_nTicksPerQuarterNote(480),
          m_nCurrentTick(0),
          m_bSyncEnabled(false),
          m_activeState(ActiveState::Inactive),
          m_bPlaying(false),
          m_bLoopEnabled(false),
          m_dLoopStart(0.0),
          m_dLoopEnd(0.0),
          m_bBeatJumping(false),
          m_dBeatJumpTarget(0.0),
          m_pTimer(nullptr),
          m_pEffectsManager(pEffectsManager) {
    // Start the event scheduling timer
    m_pTimer = new QTimer(this);
    connect(m_pTimer, &QTimer::timeout, this, &EngineMidiDeck::onTimerTick);
    m_pTimer->start(10); // 10ms resolution for MIDI events
    kLogger.debug() << "EngineMidiDeck created for group" << m_group.name();
}

EngineMidiDeck::~EngineMidiDeck() {
    if (m_pTimer) {
        m_pTimer->stop();
        m_pTimer->deleteLater();
    }
    kLogger.debug() << "EngineMidiDeck destroyed";
}

// =============================================================================
// EngineChannel overrides
// =============================================================================

void EngineMidiDeck::process(CSAMPLE* pInOut,
        const std::size_t bufferSize) {
    // MIDI decks don't process audio samples — they generate MIDI events.
    // pInOut is untouched; audio output goes through a separate MIDI-to-sound
    // path (ALSA MIDI output, MIDI controller, etc.)
    Q_UNUSED(pInOut)
    Q_UNUSED(bufferSize)
}

void EngineMidiDeck::collectFeatures(GroupFeatureState* pGroupFeatures) const {
    // Expose MIDI-specific features to Mixxx skin engine
    if (pGroupFeatures) {
        pGroupFeatures->bHasBpm = true;
        pGroupFeatures->bHasDuration = true;
        pGroupFeatures->bHasLoop = m_bLoopEnabled;
        pGroupFeatures->bHasCue = m_activeState != ActiveState::Inactive;
    }
}

EngineChannel::ActiveState EngineMidiDeck::updateActiveState() {
    if (m_bPlaying) {
        m_activeState = ActiveState::Active;
    } else if (m_activeState == ActiveState::WasActive) {
        m_activeState = ActiveState::Inactive;
    } else {
        m_activeState = ActiveState::WasActive;
    }
    return m_activeState;
}

bool EngineMidiDeck::isActive() {
    return m_activeState != ActiveState::Inactive;
}

// =============================================================================
// Syncable interface
// =============================================================================

double EngineMidiDeck::getRateRatio() const {
    return m_dRateRatio;
}

void EngineMidiDeck::setRateRatio(double rateRatio) {
    m_dRateRatio = rateRatio;
    updateTickRate();
}

double EngineMidiDeck::getSyncBpm() const {
    return m_dSyncBpm;
}

void EngineMidiDeck::setSyncBpm(double syncBpm) {
    m_dSyncBpm = syncBpm;
    if (m_dSyncBpm > 0) {
        m_bSyncEnabled = true;
        // Recalculate rate ratio from sync BPM
        m_dRateRatio = m_dBpm / m_dSyncBpm;
        updateTickRate();
    }
}

double EngineMidiDeck::getCurrentBpm() const {
    return m_dBpm;
}

void EngineMidiDeck::setCurrentBpm(double bpm) {
    if (bpm <= 0) {
        kLogger.warning() << "Invalid BPM value:" << bpm;
        return;
    }
    m_dBpm = bpm;
    if (m_bSyncEnabled && m_dSyncBpm > 0) {
        m_dRateRatio = m_dBpm / m_dSyncBpm;
        updateTickRate();
    }
}

// =============================================================================
// Transport controls
// =============================================================================

void EngineMidiDeck::play() {
    m_bPlaying = true;
    m_activeState = ActiveState::Active;
    m_pTimer->start(10);
    kLogger.debug() << "MIDI Deck playing";
}

void EngineMidiDeck::pause() {
    m_bPlaying = false;
    m_pTimer->stop();
    kLogger.debug() << "MIDI Deck paused";
}

void EngineMidiDeck::stop() {
    m_bPlaying = false;
    m_pTimer->stop();
    m_dPositionSeconds = 0.0;
    m_nCurrentTick = 0;
    kLogger.debug() << "MIDI Deck stopped";
}

void EngineMidiDeck::cue() {
    // Reset to start position
    m_dPositionSeconds = 0.0;
    m_nCurrentTick = 0;
    m_bBeatJumping = false;
    kLogger.debug() << "MIDI Deck cue'd to position 0";
}

// =============================================================================
// Position controls
// =============================================================================

double EngineMidiDeck::getPosition() const {
    return m_dPositionSeconds;
}

void EngineMidiDeck::setPosition(double positionSeconds) {
    if (positionSeconds < 0) {
        positionSeconds = 0;
    }
    if (m_bLoopEnabled && positionSeconds < m_dLoopStart) {
        positionSeconds = m_dLoopStart;
    }
    if (m_bLoopEnabled && positionSeconds > m_dLoopEnd) {
        positionSeconds = m_dLoopEnd;
    }
    m_dPositionSeconds = positionSeconds;
    m_nCurrentTick = static_cast<int>(
            positionSeconds * m_nTicksPerQuarterNote * m_dBpm / 60.0);
    m_bBeatJumping = false;
}

double EngineMidiDeck::getDuration() const {
    return m_dDurationSeconds;
}

// =============================================================================
// Beatmatching (Syncable)
// =============================================================================

void EngineMidiDeck::beatJumpForward(int beats) {
    double secondsPerBeat = 60.0 / m_dBpm;
    double jumpSeconds = beats * secondsPerBeat;
    double newPos = m_dPositionSeconds + jumpSeconds;

    // Clamp to loop region if enabled
    if (m_bLoopEnabled) {
        newPos = std::max(newPos, m_dLoopStart);
        newPos = std::min(newPos, m_dLoopEnd);
    }

    setPosition(newPos);
    kLogger.debug() << "Beat jump forward" << beats << "beats to" << newPos;
}

void EngineMidiDeck::beatJumpBackward(int beats) {
    double secondsPerBeat = 60.0 / m_dBpm;
    double jumpSeconds = beats * secondsPerBeat;
    double newPos = m_dPositionSeconds - jumpSeconds;

    // Clamp to loop region if enabled
    if (m_bLoopEnabled) {
        newPos = std::max(newPos, m_dLoopStart);
        newPos = std::min(newPos, m_dLoopEnd);
    }

    setPosition(newPos);
    kLogger.debug() << "Beat jump backward" << beats << "beats to" << newPos;
}

double EngineMidiDeck::getBeatJumpPosition() const {
    return m_dBeatJumpTarget;
}

// =============================================================================
// MIDI-specific features
// =============================================================================

int EngineMidiDeck::getTicksPerQuarterNote() const {
    return m_nTicksPerQuarterNote;
}

double EngineMidiDeck::getTrackDurationSeconds() const {
    return m_dDurationSeconds;
}

void EngineMidiDeck::setLoopStart(double seconds) {
    m_dLoopStart = seconds;
    if (m_bLoopEnabled) {
        setPosition(std::max(m_dPositionSeconds, seconds));
    }
}

void EngineMidiDeck::setLoopEnd(double seconds) {
    m_dLoopEnd = seconds;
    if (m_bLoopEnabled) {
        setPosition(std::min(m_dPositionSeconds, seconds));
    }
}

bool EngineMidiDeck::isLoopEnabled() const {
    return m_bLoopEnabled;
}

const QUrl& EngineMidiDeck::getSourceUrl() const {
    return m_url;
}

void EngineMidiDeck::loadSource(const QUrl& url) {
    m_url = url;
    m_dBpm = 120.0;
    m_dRateRatio = 1.0;
    m_dSyncBpm = 120.0;
    m_dPositionSeconds = 0.0;
    m_nCurrentTick = 0;
    m_bPlaying = false;
    m_bLoopEnabled = false;
    m_activeState = ActiveState::Inactive;
    kLogger.debug() << "Loading MIDI source:" << url.toString();
}

// =============================================================================
// Internal
// =============================================================================

void EngineMidiDeck::updateTickRate() {
    // Adjust tick rate based on rate ratio
    // Normal: 1 tick per (60/BPM/PPQN) seconds
    // At rateRatio: 1 tick per (60/BPM/PPQN/rateRatio) seconds
    // (Actual scheduling is handled by the MidiEventScheduler)
    kLogger.debug() << "Tick rate updated: BPM=" << m_dBpm
                    << " rateRatio=" << m_dRateRatio;
}

void EngineMidiDeck::onTimerTick() {
    if (!m_bPlaying) {
        return;
    }

    // In production, this would advance through the MidiEventScheduler's
    // event queue and send MIDI events via ALSA output.
    // For now, advance position based on rate ratio.
    double dt = 0.01; // 10ms timer
    double ticksPerSecond = m_dBpm * m_nTicksPerQuarterNote / 60.0 * m_dRateRatio;
    m_nCurrentTick += static_cast<int>(ticksPerSecond * dt);
    m_dPositionSeconds = static_cast<double>(m_nCurrentTick) / ticksPerSecond;

    // Check loop wrap
    if (m_bLoopEnabled && m_dPositionSeconds > m_dLoopEnd) {
        m_dPositionSeconds = m_dLoopStart;
        m_nCurrentTick = static_cast<int>(
                m_dLoopStart * m_dBpm * m_nTicksPerQuarterNote / 60.0);
        kLogger.debug() << "Loop wrap: position reset to" << m_dLoopStart;
    }
}

} // namespace mixxx
