/*
 * Fooyin
 * Copyright © 2023, Luke Taylor <LukeT1@proton.me>
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

#include "librarythreadhandler.h"

#include "internalcoresettings.h"
#include "libraryscanner.h"
#include "trackdatabasemanager.h"

#include <core/library/musiclibrary.h>
#include <utils/settings/settingsmanager.h>

#include <QBasicTimer>
#include <QThread>
#include <QTimerEvent>
#include <QUrl>

#include <deque>

using namespace std::chrono_literals;

#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
constexpr auto WriteInterval  = 1s;
constexpr auto UpdateInterval = 1s;
#else
constexpr auto WriteInterval  = 1000;
constexpr auto UpdateInterval = 1000;
#endif

namespace {
int nextRequestId()
{
    static int requestId{0};
    return requestId++;
}
} // namespace

namespace Fooyin {
struct LibraryScanRequest
{
    int id;
    ScanRequest::Type type;
    LibraryInfo library;
    QStringList dirs;
    QList<QUrl> files;
    TrackList tracks;
    bool onlyModified{true};
};

class LibraryThreadHandlerPrivate
{
public:
    LibraryThreadHandlerPrivate(LibraryThreadHandler* self, DbConnectionPoolPtr dbPool, MusicLibrary* library,
                                std::shared_ptr<PlaylistLoader> playlistLoader,
                                const std::shared_ptr<AudioLoader>& audioLoader, SettingsManager* settings);

    void scanLibrary(const LibraryScanRequest& request);
    void scanTracks(const LibraryScanRequest& request);
    void scanFiles(const LibraryScanRequest& request);
    void scanDirectory(const LibraryScanRequest& request);
    void scanPlaylist(const LibraryScanRequest& request);

    ScanRequest addLibraryScanRequest(const LibraryInfo& libraryInfo, bool onlyModified);
    ScanRequest addTracksScanRequest(const TrackList& tracks, bool onlyModified);
    ScanRequest addFilesScanRequest(const QList<QUrl>& files);
    ScanRequest addDirectoryScanRequest(const LibraryInfo& libraryInfo, const QStringList& dirs);
    ScanRequest addPlaylistRequest(const QList<QUrl>& files);

    [[nodiscard]] std::optional<LibraryScanRequest> currentRequest() const;
    void execNextRequest();

    void updateProgress(int current, const QString& file, int total);
    void finishScanRequest();
    void cancelScanRequest(int id);

    LibraryThreadHandler* m_self;

    DbConnectionPoolPtr m_dbPool;
    MusicLibrary* m_library;
    SettingsManager* m_settings;

    QThread m_thread;
    LibraryScanner m_scanner;
    TrackDatabaseManager m_trackDatabaseManager;

    QBasicTimer m_writeTimer;
    TrackList m_tracksPendingWrite;
    QBasicTimer m_updateTimer;
    TrackList m_tracksPendingUpdate;
    QBasicTimer m_playcountTimer;
    TrackList m_tracksPendingPlaycountUpdate;

    std::deque<LibraryScanRequest> m_scanRequests;
    int m_currentRequestId{-1};
    bool m_currentRequestFinished{false};
    bool m_tracksAddedToLibrary{false};
};

LibraryThreadHandlerPrivate::LibraryThreadHandlerPrivate(LibraryThreadHandler* self, DbConnectionPoolPtr dbPool,
                                                         MusicLibrary* library,
                                                         std::shared_ptr<PlaylistLoader> playlistLoader,
                                                         const std::shared_ptr<AudioLoader>& audioLoader,
                                                         SettingsManager* settings)
    : m_self{self}
    , m_dbPool{std::move(dbPool)}
    , m_library{library}
    , m_settings{settings}
    , m_scanner{m_dbPool, std::move(playlistLoader), audioLoader, m_settings}
    , m_trackDatabaseManager{m_dbPool, audioLoader, m_settings}
{
    m_scanner.setMonitorLibraries(m_settings->value<Settings::Core::Internal::MonitorLibraries>());

    m_scanner.moveToThread(&m_thread);
    m_trackDatabaseManager.moveToThread(&m_thread);

    QObject::connect(m_library, &MusicLibrary::tracksScanned, m_self, [this]() {
        m_tracksAddedToLibrary = true;
        if(m_currentRequestFinished) {
            execNextRequest();
        }
    });

    m_thread.start();

    m_settings->subscribe<Settings::Core::Internal::MonitorLibraries>(&m_scanner, &LibraryScanner::setMonitorLibraries);
}

void LibraryThreadHandlerPrivate::scanLibrary(const LibraryScanRequest& request)
{
    QMetaObject::invokeMethod(&m_scanner, [this, request]() {
        m_scanner.scanLibrary(request.library, m_library->tracks(), request.onlyModified);
    });
}

void LibraryThreadHandlerPrivate::scanTracks(const LibraryScanRequest& request)
{
    QMetaObject::invokeMethod(&m_scanner, [this, request]() {
        m_scanner.scanTracks(m_library->tracks(), request.tracks, request.onlyModified);
    });
}

void LibraryThreadHandlerPrivate::scanFiles(const LibraryScanRequest& request)
{
    QMetaObject::invokeMethod(&m_scanner,
                              [this, request]() { m_scanner.scanFiles(m_library->tracks(), request.files); });
}

void LibraryThreadHandlerPrivate::scanDirectory(const LibraryScanRequest& request)
{
    QMetaObject::invokeMethod(&m_scanner, [this, request]() {
        m_scanner.scanLibraryDirectoies(request.library, request.dirs, m_library->tracks());
    });
}

void LibraryThreadHandlerPrivate::scanPlaylist(const LibraryScanRequest& request)
{
    QMetaObject::invokeMethod(&m_scanner,
                              [this, request]() { m_scanner.scanPlaylist(m_library->tracks(), request.files); });
}

ScanRequest LibraryThreadHandlerPrivate::addLibraryScanRequest(const LibraryInfo& libraryInfo, bool onlyModified)
{
    const int id = nextRequestId();

    ScanRequest request{.type = ScanRequest::Library, .id = id, .cancel = [this, id]() {
                            cancelScanRequest(id);
                        }};

    LibraryScanRequest libraryRequest;
    libraryRequest.id           = id;
    libraryRequest.type         = ScanRequest::Library;
    libraryRequest.library      = libraryInfo;
    libraryRequest.onlyModified = onlyModified;

    m_scanRequests.emplace_back(libraryRequest);

    if(m_scanRequests.size() == 1) {
        execNextRequest();
    }

    return request;
}

ScanRequest LibraryThreadHandlerPrivate::addTracksScanRequest(const TrackList& tracks, bool onlyModified)
{
    const int id = nextRequestId();

    ScanRequest request{.type = ScanRequest::Tracks, .id = id, .cancel = [this, id]() {
                            cancelScanRequest(id);
                        }};

    LibraryScanRequest libraryRequest;
    libraryRequest.id           = id;
    libraryRequest.type         = ScanRequest::Tracks;
    libraryRequest.tracks       = tracks;
    libraryRequest.onlyModified = onlyModified;

    m_scanRequests.emplace_front(libraryRequest);

    // Track scans take precedence over library scans
    const auto currRequest = currentRequest();
    if(currRequest && currRequest->type == ScanRequest::Library) {
        m_scanner.pauseThread();
        execNextRequest();
    }
    else if(m_scanRequests.size() == 1) {
        execNextRequest();
    }

    return request;
}

ScanRequest LibraryThreadHandlerPrivate::addFilesScanRequest(const QList<QUrl>& files)
{
    const int id = nextRequestId();

    ScanRequest request{.type = ScanRequest::Files, .id = id, .cancel = [this, id]() {
                            cancelScanRequest(id);
                        }};

    LibraryScanRequest libraryRequest;
    libraryRequest.id    = id;
    libraryRequest.type  = ScanRequest::Files;
    libraryRequest.files = files;

    m_scanRequests.emplace_front(libraryRequest);

    // File scans take precedence over library and track scans
    const auto currRequest = currentRequest();
    if(currRequest && (currRequest->type == ScanRequest::Library || currRequest->type == ScanRequest::Tracks)) {
        m_scanner.pauseThread();
        execNextRequest();
    }
    else if(m_scanRequests.size() == 1) {
        execNextRequest();
    }

    return request;
}

ScanRequest LibraryThreadHandlerPrivate::addDirectoryScanRequest(const LibraryInfo& libraryInfo,
                                                                 const QStringList& dirs)
{
    const int id = nextRequestId();

    ScanRequest request{.type = ScanRequest::Library, .id = id, .cancel = [this, id]() {
                            cancelScanRequest(id);
                        }};

    LibraryScanRequest libraryRequest;
    libraryRequest.id      = id;
    libraryRequest.type    = ScanRequest::Library;
    libraryRequest.library = libraryInfo;
    libraryRequest.dirs    = dirs;

    m_scanRequests.emplace_back(libraryRequest);

    if(m_scanRequests.size() == 1) {
        execNextRequest();
    }

    return request;
}

ScanRequest LibraryThreadHandlerPrivate::addPlaylistRequest(const QList<QUrl>& files)
{
    const int id = nextRequestId();

    ScanRequest request{.type = ScanRequest::Playlist, .id = id, .cancel = [this, id]() {
                            cancelScanRequest(id);
                        }};

    LibraryScanRequest libraryRequest;
    libraryRequest.id    = id;
    libraryRequest.type  = ScanRequest::Playlist;
    libraryRequest.files = files;

    m_scanRequests.emplace_front(libraryRequest);

    // Playlist scans take precedence over library and track scans
    const auto currRequest = currentRequest();
    if(currRequest && (currRequest->type == ScanRequest::Library || currRequest->type == ScanRequest::Tracks)) {
        m_scanner.pauseThread();
        execNextRequest();
    }
    else if(m_scanRequests.size() == 1) {
        execNextRequest();
    }

    return request;
}

std::optional<LibraryScanRequest> LibraryThreadHandlerPrivate::currentRequest() const
{
    const auto requestIt = std::ranges::find_if(
        m_scanRequests, [this](const auto& request) { return request.id == m_currentRequestId; });
    if(requestIt != m_scanRequests.cend()) {
        return *requestIt;
    }
    return {};
}

void LibraryThreadHandlerPrivate::execNextRequest()
{
    if(m_scanRequests.empty()) {
        return;
    }

    const auto& request      = m_scanRequests.front();
    m_currentRequestId       = request.id;
    m_currentRequestFinished = false;
    m_tracksAddedToLibrary   = false;

    switch(request.type) {
        case(ScanRequest::Files):
            scanFiles(request);
            break;
        case(ScanRequest::Tracks):
            scanTracks(request);
            break;
        case(ScanRequest::Library):
            if(request.dirs.isEmpty()) {
                scanLibrary(request);
            }
            else {
                scanDirectory(request);
            }
            break;
        case(ScanRequest::Playlist):
            scanPlaylist(request);
            break;
    }
}

void LibraryThreadHandlerPrivate::updateProgress(int current, const QString& file, int total)
{
    ScanProgress progress;
    progress.id      = m_currentRequestId;
    progress.total   = total;
    progress.current = current;
    progress.file    = file;

    if(!m_scanRequests.empty()) {
        const auto& request = m_scanRequests.front();

        progress.type = request.type;
        progress.info = request.library;
    }

    emit m_self->progressChanged(progress);
}

void LibraryThreadHandlerPrivate::finishScanRequest()
{
    if(const auto request = currentRequest()) {
        std::erase_if(m_scanRequests,
                      [this](const auto& pendingRequest) { return pendingRequest.id == m_currentRequestId; });

        m_currentRequestFinished = true;

        if((request->type == ScanRequest::Files || request->type == ScanRequest::Playlist) && !m_tracksAddedToLibrary) {
            // Next request (if any) will be started after tracksScanned is emitted from MusicLibrary
            return;
        }
    }

    m_currentRequestId = -1;
    execNextRequest();
}

void LibraryThreadHandlerPrivate::cancelScanRequest(int id)
{
    if(m_currentRequestId == id) {
        // Will be removed in finishScanRequest
        m_scanner.stopThread();
    }
    else {
        std::erase_if(m_scanRequests, [id](const auto& request) { return request.id == id; });
    }
}

LibraryThreadHandler::LibraryThreadHandler(DbConnectionPoolPtr dbPool, MusicLibrary* library,
                                           std::shared_ptr<PlaylistLoader> playlistLoader,
                                           std::shared_ptr<AudioLoader> audioLoader, SettingsManager* settings,
                                           QObject* parent)
    : QObject{parent}
    , p{std::make_unique<LibraryThreadHandlerPrivate>(this, std::move(dbPool), library, std::move(playlistLoader),
                                                      std::move(audioLoader), settings)}
{
    QObject::connect(&p->m_trackDatabaseManager, &TrackDatabaseManager::gotTracks, this,
                     &LibraryThreadHandler::gotTracks);
    QObject::connect(&p->m_trackDatabaseManager, &TrackDatabaseManager::updatedTracks, this,
                     &LibraryThreadHandler::tracksUpdated);
    QObject::connect(&p->m_trackDatabaseManager, &TrackDatabaseManager::updatedTracksStats, this,
                     &LibraryThreadHandler::tracksStatsUpdated);
    QObject::connect(&p->m_trackDatabaseManager, &TrackDatabaseManager::writeProgress, this,
                     &LibraryThreadHandler::writeProgress);
    QObject::connect(&p->m_trackDatabaseManager, &TrackDatabaseManager::removedTracks, this,
                     &LibraryThreadHandler::tracksRemoved);
    QObject::connect(&p->m_scanner, &Worker::finished, this, [this]() { p->finishScanRequest(); });
    QObject::connect(&p->m_scanner, &LibraryScanner::progressChanged, this,
                     [this](int current, const QString& file, int total) { p->updateProgress(current, file, total); });
    QObject::connect(&p->m_scanner, &LibraryScanner::scannedTracks, this,
                     [this](const TrackList& tracks) { emit scannedTracks(p->m_currentRequestId, tracks); });
    QObject::connect(&p->m_scanner, &LibraryScanner::playlistLoaded, this,
                     [this](const TrackList& tracks) { emit playlistLoaded(p->m_currentRequestId, tracks); });
    QObject::connect(&p->m_scanner, &LibraryScanner::statusChanged, this, &LibraryThreadHandler::statusChanged);
    QObject::connect(&p->m_scanner, &LibraryScanner::scanUpdate, this, &LibraryThreadHandler::scanUpdate);
    QObject::connect(&p->m_scanner, &LibraryScanner::directoriesChanged, this,
                     [this](const LibraryInfo& libraryInfo, const QStringList& dirs) {
                         p->addDirectoryScanRequest(libraryInfo, dirs);
                     });

    QMetaObject::invokeMethod(&p->m_scanner, &Worker::initialiseThread);
    QMetaObject::invokeMethod(&p->m_trackDatabaseManager, &Worker::initialiseThread);
}

LibraryThreadHandler::~LibraryThreadHandler()
{
    p->m_scanner.stopThread();
    p->m_trackDatabaseManager.stopThread();

    p->m_thread.quit();
    p->m_thread.wait();
}

void LibraryThreadHandler::getAllTracks()
{
    QMetaObject::invokeMethod(&p->m_trackDatabaseManager, &TrackDatabaseManager::getAllTracks);
}

void LibraryThreadHandler::setupWatchers(const LibraryInfoMap& libraries, bool enabled)
{
    QMetaObject::invokeMethod(&p->m_scanner,
                              [this, libraries, enabled]() { p->m_scanner.setupWatchers(libraries, enabled); });
}

ScanRequest LibraryThreadHandler::refreshLibrary(const LibraryInfo& library)
{
    return p->addLibraryScanRequest(library, true);
}

ScanRequest LibraryThreadHandler::scanLibrary(const LibraryInfo& library)
{
    return p->addLibraryScanRequest(library, false);
}

ScanRequest LibraryThreadHandler::scanTracks(const TrackList& tracks, bool onlyModified)
{
    return p->addTracksScanRequest(tracks, onlyModified);
}

ScanRequest LibraryThreadHandler::scanFiles(const QList<QUrl>& files)
{
    return p->addFilesScanRequest(files);
}

ScanRequest LibraryThreadHandler::loadPlaylist(const QList<QUrl>& files)
{
    return p->addPlaylistRequest(files);
}

void LibraryThreadHandler::saveUpdatedTracks(const TrackList& tracks)
{
    QMetaObject::invokeMethod(&p->m_trackDatabaseManager,
                              [this, tracks]() { p->m_trackDatabaseManager.updateTracks(tracks, false); });
}

WriteRequest LibraryThreadHandler::writeUpdatedTracks(const TrackList& tracks)
{
    WriteRequest request;
    request.cancel = [this]() {
        p->m_trackDatabaseManager.stopThread();
    };

    p->m_tracksPendingWrite.insert(p->m_tracksPendingWrite.end(), tracks.cbegin(), tracks.cend());
    p->m_writeTimer.start(WriteInterval, this);

    return request;
}

WriteRequest LibraryThreadHandler::writeTrackCovers(const TrackCoverData& tracks)
{
    WriteRequest request;
    request.cancel = [this]() {
        p->m_trackDatabaseManager.stopThread();
    };

    QMetaObject::invokeMethod(&p->m_trackDatabaseManager,
                              [this, tracks]() { p->m_trackDatabaseManager.writeCovers(tracks); });

    return request;
}

void LibraryThreadHandler::saveUpdatedTrackStats(const TrackList& tracks)
{
    p->m_tracksPendingUpdate.insert(p->m_tracksPendingUpdate.end(), tracks.cbegin(), tracks.cend());
    p->m_updateTimer.start(UpdateInterval, this);
}

void LibraryThreadHandler::saveUpdatedTrackPlaycounts(const TrackList& tracks)
{
    p->m_tracksPendingPlaycountUpdate.insert(p->m_tracksPendingPlaycountUpdate.end(), tracks.cbegin(), tracks.cend());
    p->m_playcountTimer.start(UpdateInterval, this);
}

WriteRequest LibraryThreadHandler::removeUnavailbleTracks(const TrackList& tracks)
{
    WriteRequest request;
    request.cancel = [this]() {
        p->m_trackDatabaseManager.stopThread();
    };

    QMetaObject::invokeMethod(&p->m_trackDatabaseManager,
                              [this, tracks]() { p->m_trackDatabaseManager.removeUnavailbleTracks(tracks); });

    return request;
}

void LibraryThreadHandler::cleanupTracks()
{
    QMetaObject::invokeMethod(&p->m_trackDatabaseManager, &TrackDatabaseManager::cleanupTracks);
}

void LibraryThreadHandler::libraryRemoved(int id)
{
    if(p->m_scanRequests.empty()) {
        return;
    }

    const auto request = p->currentRequest();
    if(request && request->type == ScanRequest::Library && request->library.id == id) {
        p->m_scanner.stopThread();
    }
    else {
        std::erase_if(p->m_scanRequests, [id](const auto& pendingRequest) { return pendingRequest.library.id == id; });
    }
}

void LibraryThreadHandler::timerEvent(QTimerEvent* event)
{
    if(event->timerId() == p->m_writeTimer.timerId()) {
        p->m_writeTimer.stop();
        QMetaObject::invokeMethod(&p->m_trackDatabaseManager, [this, tracks = p->m_tracksPendingWrite]() {
            p->m_trackDatabaseManager.updateTracks(tracks, true);
        });
        p->m_tracksPendingWrite.clear();
    }
    else if(event->timerId() == p->m_updateTimer.timerId()) {
        p->m_updateTimer.stop();
        QMetaObject::invokeMethod(&p->m_trackDatabaseManager, [this, tracks = p->m_tracksPendingUpdate]() {
            p->m_trackDatabaseManager.updateTrackStats(tracks, false);
        });
        p->m_tracksPendingUpdate.clear();
    }
    else if(event->timerId() == p->m_playcountTimer.timerId()) {
        p->m_playcountTimer.stop();
        QMetaObject::invokeMethod(&p->m_trackDatabaseManager, [this, tracks = p->m_tracksPendingPlaycountUpdate]() {
            p->m_trackDatabaseManager.updateTrackStats(tracks, true);
        });
        p->m_tracksPendingPlaycountUpdate.clear();
    }

    QObject::timerEvent(event);
}
} // namespace Fooyin

#include "moc_librarythreadhandler.cpp"
