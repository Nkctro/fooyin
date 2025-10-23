/*
 * Fooyin
 * Copyright © 2024, Luke Taylor <LukeT1@proton.me>
 *
 * Fooyin is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Fooyin is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Fooyin.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "ebur128scanner.h"

#include <core/constants.h>
#include <core/coresettings.h>
#include <core/engine/audioconverter.h>

#include "rgscannerdefs.h"
#include "rgscanmemorycache.h"

#include <QFile>
#include <QFuture>
#include <QFutureWatcher>
#include <QLoggingCategory>
#include <QThreadPool>
#include <QString>
#include <QtConcurrentMap>

#include <algorithm>
#include <memory>
#include <mutex>

Q_LOGGING_CATEGORY(EBUR128, "fy.ebur128")

constexpr auto ReferenceLUFS  = -18;
constexpr auto BufferSize     = 10240;
constexpr auto SingleAlbumKey = "Album";

namespace {
int configuredThreadLimit()
{
    return Fooyin::RGScanner::currentThreadLimit();
}

QThreadPool* replayGainThreadPool()
{
    static QThreadPool pool;
    static std::once_flag once;
    std::call_once(once, [] { pool.setExpiryTimeout(-1); });
    pool.setMaxThreadCount(configuredThreadLimit());
    return &pool;
}
}

namespace Fooyin::RGScanner {
Ebur128Scanner::Ebur128Scanner(std::shared_ptr<AudioLoader> audioLoader, QObject* parent)
    : RGWorker{parent}
    , m_audioLoader{std::move(audioLoader)}
    , m_watcher{nullptr}
    , m_decoder{nullptr}
    , m_runningWatchers{0}
{ }

void Ebur128Scanner::closeThread()
{
    RGWorker::closeThread();

    QMetaObject::invokeMethod(this, [this]() {
        auto cancelFuture = [](QFutureWatcher<void>* watcher) {
            if(watcher) {
                watcher->cancel();
                watcher->waitForFinished();
            }
        };

        cancelFuture(m_watcher);
        for(const auto& [_, watcher] : m_albumWatchers) {
            cancelFuture(watcher);
        }

        emit closed();
    });
}

void Ebur128Scanner::calculatePerTrack(const TrackList& tracks, bool truePeak)
{
    setState(Running);

    qCDebug(EBUR128) << "Calculating RG using ebur128 for" << tracks.size() << "tracks";

    m_watcher       = new QFutureWatcher<void>(this);
    m_tracks        = tracks;
    m_scannedTracks = tracks;

    QObject::connect(m_watcher, &QFutureWatcher<void>::progressValueChanged, this, [this](const int val) {
        if(val >= 0 && std::cmp_less(val, m_tracks.size())) {
            emit startingCalculation(m_tracks.at(val).prettyFilepath());
        }
    });

    auto future = QtConcurrent::map(replayGainThreadPool(), m_scannedTracks, [this, truePeak](Track& track) {
        scanTrack(track, truePeak);
    });

    m_watcher->setFuture(future);
    m_runningWatchers.fetch_add(1, std::memory_order_acquire);

    future.then(this, [this]() {
        if(mayRun()) {
            qCDebug(EBUR128) << "Finished calculating RG for" << m_scannedTracks.size() << "tracks";
            emit calculationFinished(m_scannedTracks);
        }
        if(m_runningWatchers.fetch_sub(1, std::memory_order_release) <= 1) {
            emit finished();
        }
        setState(Idle);
    });
}

void Ebur128Scanner::calculateAsAlbum(const TrackList& tracks, bool truePeak)
{
    setState(Running);

    qCDebug(EBUR128) << "Calculating RG using ebur128 for" << tracks.size() << "tracks";

    m_watcher       = new QFutureWatcher<void>(this);
    m_tracks        = tracks;
    m_scannedTracks = tracks;

    QObject::connect(m_watcher, &QFutureWatcher<void>::progressValueChanged, this, [this](const int val) {
        if(val >= 0 && std::cmp_less(val, m_tracks.size())) {
            emit startingCalculation(m_tracks.at(val).prettyFilepath());
        }
    });

    auto future = QtConcurrent::map(replayGainThreadPool(), m_scannedTracks, [this, truePeak](Track& track) {
        scanTrack(track, truePeak, QString::fromLatin1(SingleAlbumKey));
    });

    m_watcher->setFuture(future);
    m_runningWatchers.fetch_add(1, std::memory_order_acquire);

    future.then(this, [this]() {
        const auto albumState = m_albumStates.find(QString::fromLatin1(SingleAlbumKey));
        if(albumState != m_albumStates.cend()) {
            const auto& trackStates = albumState->second;
            std::vector<ebur128_state*> states;
            std::ranges::transform(trackStates, std::back_inserter(states),
                                   [](const auto& state) { return state.get(); });

            double albumGain{Constants::InvalidGain};
            if(ebur128_loudness_global_multiple(states.data(), states.size(), &albumGain) == EBUR128_SUCCESS) {
                albumGain = ReferenceLUFS - albumGain;
            }

            const float albumPeak
                = std::ranges::max_element(m_scannedTracks, std::ranges::less{}, &Track::rgTrackPeak)->rgTrackPeak();

            for(Track& track : m_scannedTracks) {
                track.setRGAlbumGain(static_cast<float>(albumGain));
                track.setRGAlbumPeak(albumPeak);
            }
        }

        if(mayRun()) {
            qCDebug(EBUR128) << "Finished calculating RG for" << m_scannedTracks.size() << "tracks";
            emit calculationFinished(m_scannedTracks);
        }
        if(m_runningWatchers.fetch_sub(1, std::memory_order_release) <= 1) {
            emit finished();
        }
        setState(Idle);
    });
}

void Ebur128Scanner::calculateByAlbumTags(const TrackList& tracks, const QString& groupScript, bool truePeak)
{
    setState(Running);

    qCDebug(EBUR128) << "Calculating RG using ebur128 for" << tracks.size() << "tracks";

    for(const auto& track : tracks) {
        const QString album = m_parser.evaluate(groupScript, track);
        m_albums[album].push_back(track);
    }

    m_currentAlbum = m_albums.begin();
    scanAlbum(truePeak);
}

void Ebur128Scanner::scanTrack(Track& track, bool truePeak, const QString& album)
{
    if(!mayRun()) {
        return;
    }

    auto decoder = m_audioLoader->decoderForTrack(track);
    if(!decoder) {
        return;
    }

    AudioSource source;
    source.filepath = track.filepath();
    source.device   = nullptr;

    MemoryScopedReservation staged;
    std::unique_ptr<QFile> file;

    if(!track.isInArchive()) {
        if(staged.load(source.filepath)) {
            source.device = staged.device();
        }
    }

    if(!source.device) {
        file = std::make_unique<QFile>(source.filepath);
        if(!file->open(QIODevice::ReadOnly)) {
            qCWarning(EBUR128) << "Failed to open" << source.filepath;
            return;
        }
        source.device = file.get();
    }

    auto format = decoder->init(source, track, AudioDecoder::NoSeeking | AudioDecoder::NoInfiniteLooping);
    if(!format) {
        return;
    }

    format->setSampleFormat(SampleFormat::F64);
    decoder->start();

    EburStatePtr state{ebur128_init(format->channelCount(), format->sampleRate(),
                                    EBUR128_MODE_I | (truePeak ? EBUR128_MODE_TRUE_PEAK : EBUR128_MODE_SAMPLE_PEAK))};

    AudioBuffer buffer;
    while((buffer = decoder->readBuffer(BufferSize)).isValid()) {
        if(!mayRun()) {
            return;
        }

        buffer = Audio::convert(buffer, *format);
        if(ebur128_add_frames_double(state.get(), reinterpret_cast<double*>(buffer.data()), buffer.frameCount())
           != EBUR128_SUCCESS) {
            break;
        }
    }

    if(!mayRun()) {
        return;
    }

    double trackGain{Constants::InvalidGain};
    if(ebur128_loudness_global(state.get(), &trackGain) == EBUR128_SUCCESS) {
        trackGain = ReferenceLUFS - trackGain;
        track.setRGTrackGain(static_cast<float>(trackGain));
    }

    double trackPeak{Constants::InvalidPeak};
    const auto channels = static_cast<unsigned int>(format->channelCount());

    if(truePeak) {
        for(unsigned i{0}; i < channels; ++i) {
            double channelPeak{Constants::InvalidPeak};
            if(ebur128_true_peak(state.get(), i, &channelPeak) == EBUR128_SUCCESS) {
                trackPeak = std::max(trackPeak, channelPeak);
            }
        }
    }
    else {
        for(unsigned i{0}; i < channels; ++i) {
            double channelPeak{Constants::InvalidPeak};
            if(ebur128_sample_peak(state.get(), i, &channelPeak) == EBUR128_SUCCESS) {
                trackPeak = std::max(trackPeak, channelPeak);
            }
        }
    }

    track.setRGTrackPeak(static_cast<float>(trackPeak));

    if(!album.isEmpty()) {
        const std::scoped_lock lock{m_mutex};
        m_albumStates[album].emplace_back(std::move(state));
    }
}

void Ebur128Scanner::scanAlbum(bool truePeak)
{
    if(m_currentAlbum == m_albums.cend()) {
        if(mayRun()) {
            for(const auto& [_, tracks] : m_albums) {
                m_scannedTracks.insert(m_scannedTracks.end(), tracks.cbegin(), tracks.cend());
            }
            qCDebug(EBUR128) << "Finished calculating RG for" << m_scannedTracks.size() << "tracks";
            emit calculationFinished(m_scannedTracks);
        }
        if(m_runningWatchers.fetch_sub(1, std::memory_order_release) <= 1) {
            emit finished();
        }
        setState(Idle);
        return;
    }

    const auto album = m_currentAlbum->first;
    m_tracks         = m_currentAlbum->second;

    auto albumFuture = QtConcurrent::map(m_currentAlbum->second,
                                         [this, truePeak, album](Track& track) { scanTrack(track, truePeak, album); });

    auto* albumWatcher = new QFutureWatcher<void>(this);
    m_albumWatchers.emplace(album, albumWatcher);

    QObject::connect(albumWatcher, &QFutureWatcher<void>::progressValueChanged, this, [this](const int val) {
        if(val >= 0 && std::cmp_less(val, m_tracks.size())) {
            emit startingCalculation(m_tracks.at(val).prettyFilepath());
        }
    });

    QObject::connect(albumWatcher, &QFutureWatcher<void>::finished, this, [this, truePeak, album]() {
        const auto albumState = m_albumStates.find(album);
        if(albumState != m_albumStates.cend()) {
            const auto& trackStates = albumState->second;
            std::vector<ebur128_state*> states;
            std::ranges::transform(trackStates, std::back_inserter(states),
                                   [](const auto& state) { return state.get(); });

            double albumGain{Constants::InvalidGain};
            if(ebur128_loudness_global_multiple(states.data(), states.size(), &albumGain) == EBUR128_SUCCESS) {
                albumGain = ReferenceLUFS - albumGain;
            }

            auto& albumTracks = m_currentAlbum->second;

            const float albumPeak
                = std::ranges::max_element(albumTracks, std::ranges::less{}, &Track::rgTrackPeak)->rgTrackPeak();

            for(Track& track : albumTracks) {
                track.setRGAlbumGain(static_cast<float>(albumGain));
                track.setRGAlbumPeak(albumPeak);
            }

            albumState->second.clear();
        }

        ++m_currentAlbum;
        scanAlbum(truePeak);
    });

    albumWatcher->setFuture(albumFuture);
    m_runningWatchers.fetch_add(1, std::memory_order_acquire);
}
} // namespace Fooyin::RGScanner
