#pragma once

#include <memory>
#include <QString>
#include <QStringList>

#include "sources/soundsourceprovider.h"

QT_FORWARD_DECLARE_CLASS(QUrl)

namespace mixxx {

// MIDI-specific SoundSource provider
// Implements the Mixxx SoundSourceProvider interface for .mid files
class SoundSourceMidi final : public SoundSource {
  public:
    explicit SoundSourceMidi(const QUrl& url);
    ~SoundSourceMidi() override;

    void close() override;

  protected:
    OpenResult tryOpen(
            OpenMode mode,
            const OpenParams& params) override;

    ReadableSampleFrames readSampleFramesClamped(
            const WritableSampleFrames& sampleFrames) override;

  private:
    // Track metadata (no audio samples — MIDI is event-based)
    double m_dBpm;
    int m_nTicksPerQuarterNote;
    double m_dDurationSeconds;
    bool m_bOpened;
};

// Provider factory for MIDI files
class SoundSourceProviderMidi : public SoundSourceProvider {
  public:
    static const QString kDisplayName;
    static const QStringList kSupportedFileTypes;

    QString getDisplayName() const override {
        return kDisplayName;
    }

    QStringList getSupportedFileTypes() const override {
        return kSupportedFileTypes;
    }

    SoundSourceProviderPriority getPriorityHint(
            const QString& supportedFileType) const override;

    SoundSourcePointer newSoundSource(const QUrl& url) override {
        return newSoundSourceFromUrl<SoundSourceMidi>(url);
    }
};

} // namespace mixxx
