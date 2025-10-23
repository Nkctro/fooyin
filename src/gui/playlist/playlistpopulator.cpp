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

#include "playlistpopulator.h"

#include "playlistitemmodels.h"
#include "playlistpreset.h"
#include "playlistscriptregistry.h"

#include <core/player/playercontroller.h>

#include <QTimer>

#include <algorithm>
#include <ranges>

namespace Fooyin {
class PlaylistPopulatorPrivate
{
public:
    explicit PlaylistPopulatorPrivate(PlaylistPopulator* self, PlayerController* playerController)
        : m_self{self}
        , m_playerController{playerController}
        , m_registry{new PlaylistScriptRegistry()}
        , m_parser{m_registry}
    { }

    void reset();

    PlaylistItem* getOrInsertItem(const UId& key, PlaylistItem::ItemType type, const Data& item, PlaylistItem* parent,
                                  const Md5Hash& baseKey);

    void updateContainers();

    void iterateHeader(const Track& track, PlaylistItem*& parent, int index);
    void iterateSubheaders(const Track& track, PlaylistItem*& parent, int index);
    void evaluateTrackScript(RichScript& script, const Track& track);
    PlaylistItem* iterateTrack(const PlaylistTrack& track, int index);

    void runBatch(int size, int index);
    void runTracksGroup(const std::map<int, PlaylistTrackList>& tracks);

    PlaylistPopulator* m_self;
    PlayerController* m_playerController;

    PlaylistPreset m_currentPreset;
    PlaylistColumnList m_columns;

    PlaylistScriptRegistry* m_registry;
    ScriptParser m_parser;
    ScriptFormatter m_formatter;

    int m_preloadCount{2000};
    int m_trackDepth{0};
    Md5Hash m_prevBaseHeaderKey;
    UId m_prevHeaderKey;
    int m_prevIndex{0};
    std::vector<Md5Hash> m_prevBaseSubheaderKey;
    std::vector<UId> m_prevSubheaderKey;

    std::vector<PlaylistContainerItem> m_subheaders;

    PlaylistItem m_root;
    ItemKeyMap m_allItems;
    PendingData m_data;
    std::vector<UId> m_batchKeys;
    using ContainerKeyMap = std::unordered_map<UId, PlaylistContainerItem*, UId::UIdHash>;
    ContainerKeyMap m_headers;
    PlaylistTrackList m_pendingTracks;
};

void PlaylistPopulatorPrivate::reset()
{
    m_data.clear();
    m_headers.clear();
    m_trackDepth = 0;
    m_prevBaseSubheaderKey.clear();
    m_prevSubheaderKey.clear();
    m_prevBaseHeaderKey = nullptr;
    m_prevHeaderKey     = {};
    m_allItems.clear();
    m_batchKeys.clear();
    m_root = PlaylistItem{};
}
PlaylistItem* PlaylistPopulatorPrivate::getOrInsertItem(const UId& key, PlaylistItem::ItemType type, const Data& item,
                                                        PlaylistItem* parent, const Md5Hash& baseKey)
{
    auto* parentItem = parent;

    auto [storageIt, inserted] = m_allItems.try_emplace(key, PlaylistItem{type, item, parentItem});
    PlaylistItem& storageItem  = storageIt->second;

    if(inserted) {
        storageItem.setBaseKey(baseKey);
        storageItem.setKey(key);
    }
    else {
        storageItem.setData(item);
    }

    const UId parentKey = parentItem ? parentItem->key() : UId{};

    if(!storageItem.pending()) {
        storageItem.setPending(true);
        m_batchKeys.push_back(key);
        m_data.nodes[parentKey].push_back(key);
        if(type != PlaylistItem::Track) {
            m_data.containerOrder.push_back(key);
        }
    }

    m_data.items.insert_or_assign(key, storageItem);
    return &storageItem;
}

void PlaylistPopulatorPrivate::updateContainers()
{
    for(const auto& [key, container] : m_headers) {
        container->updateGroupText(&m_parser, &m_formatter);
    }
}

void PlaylistPopulatorPrivate::iterateHeader(const Track& track, PlaylistItem*& parent, int index)
{
    HeaderRow row{m_currentPreset.header};
    if(!row.isValid()) {
        return;
    }

    auto evaluateBlocks = [this, track](RichScript& script) -> QString {
        script.text.clear();
        const auto evalScript = m_parser.evaluate(script.script, track);
        if(!evalScript.isEmpty()) {
            script.text = m_formatter.evaluate(evalScript);
        }
        return evalScript;
    };

    auto generateHeaderKey = [&row, &evaluateBlocks]() {
        return Utils::generateMd5Hash(evaluateBlocks(row.title), evaluateBlocks(row.subtitle),
                                      evaluateBlocks(row.sideText), evaluateBlocks(row.info));
    };

    const auto baseKey = generateHeaderKey();
    UId key{UId::create()};
    if(m_prevHeaderKey.isValid() && m_prevBaseHeaderKey == baseKey && index == m_prevIndex + 1) {
        key = m_prevHeaderKey;
    }
    m_prevBaseHeaderKey = baseKey;
    m_prevHeaderKey     = key;

    if(!m_headers.contains(key)) {
        PlaylistContainerItem header{m_currentPreset.header.simple};
        header.setTitle(row.title);
        header.setSubtitle(row.subtitle);
        header.setSideText(row.sideText);
        header.setInfo(row.info);
        header.setRowHeight(row.rowHeight);
        header.calculateSize();

        auto* headerItem      = getOrInsertItem(key, PlaylistItem::Header, header, parent, baseKey);
        auto& headerContainer = std::get<1>(headerItem->data());
        m_headers.emplace(key, &headerContainer);
    }
    PlaylistContainerItem* header = m_headers.at(key);
    header->addTrack(track);
    m_data.trackParents[track.id()].push_back(key);

    auto* headerItem = &m_allItems.at(key);
    parent           = headerItem;
    ++m_trackDepth;
}

void PlaylistPopulatorPrivate::iterateSubheaders(const Track& track, PlaylistItem*& parent, int index)
{
    for(auto& subheader : m_currentPreset.subHeaders) {
        const auto leftScript    = m_parser.evaluate(subheader.leftText.script, track);
        subheader.leftText.text  = m_formatter.evaluate(leftScript);
        const auto rightScript   = m_parser.evaluate(subheader.rightText.script, track);
        subheader.rightText.text = m_formatter.evaluate(rightScript);

        PlaylistContainerItem currentContainer{false};
        currentContainer.setTitle(subheader.leftText);
        currentContainer.setSubtitle(subheader.rightText);
        currentContainer.setRowHeight(subheader.rowHeight);
        currentContainer.calculateSize();
        m_subheaders.push_back(currentContainer);
    }

    const int subheaderCount = static_cast<int>(m_subheaders.size());
    m_prevSubheaderKey.resize(subheaderCount);
    m_prevBaseSubheaderKey.resize(subheaderCount);

    auto generateSubheaderKey = [](const PlaylistContainerItem& subheader) {
        QString subheaderKey;
        for(const auto& block : subheader.title().text.blocks) {
            subheaderKey += block.text;
        }
        for(const auto& block : subheader.subtitle().text.blocks) {
            subheaderKey += block.text;
        }
        return subheaderKey;
    };

    for(int i{0}; const auto& subheader : m_subheaders) {
        const QString subheaderKey = generateSubheaderKey(subheader);

        if(subheaderKey.isEmpty()) {
            m_prevBaseSubheaderKey[i] = {};
            m_prevSubheaderKey[i]     = {};
            continue;
        }

        const auto baseKey = Utils::generateMd5Hash(parent->baseKey(), subheaderKey);
        UId key{UId::create()};
        if(static_cast<int>(m_prevSubheaderKey.size()) > i && m_prevBaseSubheaderKey.at(i) == baseKey
           && index == m_prevIndex + 1) {
            key = m_prevSubheaderKey.at(i);
        }
        m_prevBaseSubheaderKey[i] = baseKey;
        m_prevSubheaderKey[i]     = key;

        if(!m_headers.contains(key)) {
            auto* subheaderItem      = getOrInsertItem(key, PlaylistItem::Subheader, subheader, parent, baseKey);
            auto& subheaderContainer = std::get<1>(subheaderItem->data());
            m_headers.emplace(key, &subheaderContainer);
        }
        PlaylistContainerItem* subheaderContainer = m_headers.at(key);
        subheaderContainer->addTrack(track);
        m_data.trackParents[track.id()].push_back(key);

        auto* subheaderItem = &m_allItems.at(key);
        parent              = subheaderItem;
        ++i;
        ++m_trackDepth;
    }

    m_subheaders.clear();
}

void PlaylistPopulatorPrivate::evaluateTrackScript(RichScript& script, const Track& track)
{
    script.text.clear();
    const auto evalScript = m_parser.evaluate(script.script, track);
    if(!evalScript.isEmpty()) {
        script.text = m_formatter.evaluate(evalScript);
    }
}

PlaylistItem* PlaylistPopulatorPrivate::iterateTrack(const PlaylistTrack& track, int index)
{
    PlaylistItem* parent = &m_root;

    iterateHeader(track.track, parent, index);
    iterateSubheaders(track.track, parent, index);

    if(!m_currentPreset.track.isValid()) {
        return nullptr;
    }

    m_registry->setTrackProperties(index, m_trackDepth);

    TrackRow trackRow{m_currentPreset.track};
    PlaylistTrackItem playlistTrack;

    if(!m_columns.empty()) {
        for(const auto& column : m_columns) {
            const auto evalScript = m_parser.evaluate(column.field, track.track);
            trackRow.columns.emplace_back(column.field, m_formatter.evaluate(evalScript));
        }
        playlistTrack = {trackRow.columns, track};
    }
    else {
        evaluateTrackScript(trackRow.leftText, track.track);
        evaluateTrackScript(trackRow.rightText, track.track);

        playlistTrack = {trackRow.leftText, trackRow.rightText, track};
    }

    playlistTrack.setRowHeight(trackRow.rowHeight);
    playlistTrack.setDepth(m_trackDepth);
    playlistTrack.calculateSize();

    const auto baseKey
        = Utils::generateMd5Hash(parent->key().toString(UId::Id128), track.track.hash(), QString::number(index));
    const UId key{UId::create()};

    auto* trackItem = getOrInsertItem(key, PlaylistItem::Track, playlistTrack, parent, baseKey);
    m_data.trackParents[track.track.id()].push_back(key);

    m_trackDepth = 0;
    m_prevIndex  = index;
    return trackItem;
}

void PlaylistPopulatorPrivate::runBatch(int size, int index)
{
    if(size <= 0) {
        return;
    }

    auto tracksBatch = std::ranges::views::take(m_pendingTracks, size);

    for(const PlaylistTrack& track : tracksBatch) {
        if(!m_self->mayRun()) {
            m_data.items.clear();
            m_batchKeys.clear();
            return;
        }
        iterateTrack(track, index++);
    }

    updateContainers();

    if(!m_self->mayRun()) {
        m_data.items.clear();
        m_batchKeys.clear();
        m_data.nodes.clear();
        m_data.containerOrder.clear();
        m_data.trackParents.clear();
        m_data.indexNodes.clear();
        return;
    }

    PendingData payload;
    payload.playlistId     = m_data.playlistId;
    payload.parent         = m_data.parent;
    payload.row            = m_data.row;
    payload.nodes          = std::move(m_data.nodes);
    payload.containerOrder = std::move(m_data.containerOrder);
    payload.trackParents   = std::move(m_data.trackParents);
    payload.indexNodes     = std::move(m_data.indexNodes);

    payload.items.reserve(m_batchKeys.size());
    for(const auto& key : m_batchKeys) {
        auto it = m_data.items.find(key);
        if(it != m_data.items.end()) {
            payload.items.emplace(key, std::move(it->second));
        }
    }

    emit m_self->populated(payload);

    for(const auto& key : m_batchKeys) {
        auto it = m_allItems.find(key);
        if(it != m_allItems.end() && it->second.type() == PlaylistItem::Track) {
            m_allItems.erase(it);
        }
    }

    auto tracksToKeep = std::ranges::views::drop(m_pendingTracks, size);
    PlaylistTrackList tempTracks;
    std::ranges::copy(tracksToKeep, std::back_inserter(tempTracks));
    m_pendingTracks = std::move(tempTracks);

    m_data.items.clear();
    m_data.parent.clear();
    m_data.row = -1;
    m_batchKeys.clear();

    m_data.nodes.clear();
    m_data.containerOrder.clear();
    m_data.trackParents.clear();
    m_data.indexNodes.clear();

    const auto remaining = static_cast<int>(m_pendingTracks.size());
    if(remaining > 0) {
        const int nextBatchSize = m_preloadCount > 0 ? std::min(m_preloadCount, remaining) : remaining;
        runBatch(nextBatchSize, index);
    }
}

void PlaylistPopulatorPrivate::runTracksGroup(const std::map<int, PlaylistTrackList>& tracks)
{
    for(const auto& [index, trackGroup] : tracks) {
        std::vector<UId> trackKeys;

        int trackIndex{index};

        for(const PlaylistTrack& track : trackGroup) {
            if(!m_self->mayRun()) {
                m_data.items.clear();
                m_batchKeys.clear();
                return;
            }
            if(const auto* trackItem = iterateTrack(track, trackIndex++)) {
                trackKeys.push_back(trackItem->key());
            }
        }
        m_data.indexNodes.emplace(index, trackKeys);
    }

    updateContainers();

    if(!m_self->mayRun()) {
        m_data.items.clear();
        m_batchKeys.clear();
        m_data.nodes.clear();
        m_data.containerOrder.clear();
        m_data.trackParents.clear();
        m_data.indexNodes.clear();
        return;
    }

    PendingData payload;
    payload.playlistId     = m_data.playlistId;
    payload.parent         = m_data.parent;
    payload.row            = m_data.row;
    payload.nodes          = std::move(m_data.nodes);
    payload.containerOrder = std::move(m_data.containerOrder);
    payload.trackParents   = std::move(m_data.trackParents);
    payload.indexNodes     = std::move(m_data.indexNodes);

    payload.items.reserve(m_batchKeys.size());
    for(const auto& key : m_batchKeys) {
        auto it = m_data.items.find(key);
        if(it != m_data.items.end()) {
            payload.items.emplace(key, std::move(it->second));
        }
    }

    emit m_self->populatedTrackGroup(payload);

    for(const auto& key : m_batchKeys) {
        auto it = m_allItems.find(key);
        if(it != m_allItems.end() && it->second.type() == PlaylistItem::Track) {
            m_allItems.erase(it);
        }
    }

    m_data.items.clear();
    m_data.parent.clear();
    m_data.row = -1;
    m_batchKeys.clear();

    m_data.nodes.clear();
    m_data.containerOrder.clear();
    m_data.trackParents.clear();
    m_data.indexNodes.clear();
}

PlaylistPopulator::PlaylistPopulator(PlayerController* playerController, QObject* parent)
    : Worker{parent}
    , p{std::make_unique<PlaylistPopulatorPrivate>(this, playerController)}
{
    qRegisterMetaType<PendingData>();
}

PlaylistPopulator::~PlaylistPopulator() = default;

void PlaylistPopulator::setFont(const QFont& font)
{
    p->m_formatter.setBaseFont(font);
}

void PlaylistPopulator::setUseVarious(bool enabled)
{
    p->m_registry->setUseVariousArtists(enabled);
}

void PlaylistPopulator::setPreloadCount(int count)
{
    p->m_preloadCount = count;
}

void PlaylistPopulator::run(Playlist* playlist, const PlaylistPreset& preset, const PlaylistColumnList& columns,
                            const PlaylistTrackList& tracks)
{
    setState(Running);

    p->reset();

    if(playlist) {
        p->m_data.playlistId = playlist->id();
    }
    p->m_currentPreset = preset;
    p->m_columns       = columns;
    p->m_pendingTracks = tracks;
    p->m_registry->setup(playlist, p->m_playerController->playbackQueue());

    const int preloadCount = p->m_preloadCount > 0 ? p->m_preloadCount : static_cast<int>(tracks.size());
    p->runBatch(preloadCount, 0);

    emit finished();

    setState(Idle);
}

void PlaylistPopulator::runTracks(Playlist* playlist, const PlaylistPreset& preset, const PlaylistColumnList& columns,
                                  const std::map<int, PlaylistTrackList>& tracks)
{
    setState(Running);

    p->reset();

    if(playlist) {
        p->m_data.playlistId = playlist->id();
    }
    p->m_currentPreset = preset;
    p->m_columns       = columns;
    p->m_registry->setup(playlist, p->m_playerController->playbackQueue());

    p->runTracksGroup(tracks);

    setState(Idle);
}

void PlaylistPopulator::updateTracks(Playlist* playlist, const PlaylistPreset& preset,
                                     const PlaylistColumnList& columns, const std::set<int>& columnsToUpdate,
                                     const TrackItemMap& tracks)
{
    setState(Running);

    p->m_currentPreset = preset;
    p->m_registry->setup(playlist, p->m_playerController->playbackQueue());

    ItemList updatedTracks;

    for(const auto& [track, item] : tracks) {
        PlaylistTrackItem& trackData = std::get<0>(item.data());

        trackData.setTrack(track);
        p->m_registry->setTrackProperties(trackData.track().indexInPlaylist, trackData.depth());

        if(!columnsToUpdate.empty()) {
            std::vector<RichScript> trackColumns;
            for(int i{0}; const auto& column : columns) {
                if(columnsToUpdate.contains(i)) {
                    const auto evalScript = p->m_parser.evaluate(column.field, track.track);
                    trackColumns.emplace_back(column.field, p->m_formatter.evaluate(evalScript));
                }
                else {
                    trackColumns.emplace_back(trackData.column(i));
                }
                ++i;
            }
            trackData.setColumns(trackColumns);
        }
        else {
            RichScript trackLeft{preset.track.leftText};
            RichScript trackRight{preset.track.rightText};

            p->evaluateTrackScript(trackLeft, track.track);
            p->evaluateTrackScript(trackRight, track.track);

            trackData.setLeftRight(trackLeft, trackRight);
        }

        updatedTracks.push_back(item);
    }

    emit tracksUpdated(updatedTracks, columnsToUpdate);

    setState(Idle);
}

void PlaylistPopulator::updateHeaders(const ItemList& headers)
{
    setState(Running);

    ItemKeyMap updatedHeaders;

    for(const PlaylistItem& item : headers) {
        PlaylistContainerItem& header = std::get<1>(item.data());
        header.updateGroupText(&p->m_parser, &p->m_formatter);
        updatedHeaders.emplace(item.key(), item);
    }

    emit headersUpdated(updatedHeaders);

    setState(Idle);
}
} // namespace Fooyin

#include "moc_playlistpopulator.cpp"
