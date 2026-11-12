#include "sources/sound sourcemidi.h"

#include <QFile>

#include "util/logger.h"

namespace mixxx {

namespace {

const Logger kLogger("SoundSourceMidi");

} // anonymous namespace

//static
const QString SoundSourceProviderMidi::kDisplayName = QStringLiteral("MIDI File");

//static
const QStringList SoundSourceProviderMidi::kSupportedFileTypes = {
        QStringLiteral("mid"),
        QStringLiteral("midi"),
};

SoundSourceProviderPriority SoundSourceProviderMidi::getPriorityHint(
        const QString& /*supportedFileType*/) const {
    // Default priority — MIDI is a niche format
    return SoundSourceProviderPriority::Lower;
}

// =============================================================================
// SoundSourceMidi
// =============================================================================

SoundSourceMidi::SoundSourceMidi(const QUrl& url)
        : SoundSource(url),
          m_dBpm(120.0),
          m_nTicksPerQuarterNote(480),
          m_dDurationSeconds(0.0),
          m_bOpened(false) {
}

SoundSourceMidi::~SoundSourceMidi() {
    close();
}

void SoundSourceMidi::close() {
    m_bOpened = false;
}

SoundSource::OpenResult SoundSourceMidi::tryOpen(
        OpenMode /*mode*/,
        const OpenParams& /*config*/) {
    // Load MIDI file using our prototype parser
    // (In production, this would link to the prototype library)
    // For now, set reasonable defaults — the full parser will be
    // linked during Mixxx build.
    QFile file(getLocalFileName());
    if (!file.open(QFile::ReadOnly)) {
        kLogger.warning()
                << "Failed to open MIDI file"
                << getUrlString();
        return OpenResult::Failed;
    }

    // Read MIDI header to get ticks-per-quarter-note
    QByteArray header = file.read(14);
    if (header.size() < 14 || header.left(4) != "MThd") {
        kLogger.warning()
                << "Invalid MIDI file header"
                << getUrlString();
        return OpenResult::Aborted;
    }

    // Parse ticks per quarter note (bytes 10-13)
    m_nTicksPerQuarterNote = (header[10] << 8) | header[11];

    // Default BPM will be resolved by full parser
    m_dBpm = 120.0;
    m_dDurationSeconds = 0.0;

    m_bOpened = true;
    return OpenResult::Success;
}

ReadableSampleFrames SoundSourceMidi::readSampleFramesClamped(
        const WritableSampleFrames& /*sampleFrames*/) {
    // MIDI files don't contain audio samples — they contain events.
    // This returns zero sample frames; the EngineMidiDeck handles
    // MIDI event playback separately.
    return ReadableSampleFrames::zeroFrames();
}

} // namespace mixxx
