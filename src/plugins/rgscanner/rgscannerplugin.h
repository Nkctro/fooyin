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

#pragma once

#include <core/plugins/coreplugin.h>
#include <core/plugins/plugin.h>
#include <core/track.h>
#include <gui/plugins/guiplugin.h>

class QDialog;
class QString;

namespace Fooyin::RGScanner {
class RGScannerPlugin : public QObject,
                        public Plugin,
                        public CorePlugin,
                        public GuiPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.fooyin.fooyin.plugin/1.0" FILE "rgscanner.json")
    Q_INTERFACES(Fooyin::Plugin Fooyin::CorePlugin Fooyin::GuiPlugin)

public:
    void initialise(const CorePluginContext& context) override;
    void initialise(const GuiPluginContext& context) override;

private:
    enum class RGScanType : uint8_t
    {
        Track = 0,
        SingleAlbum,
        Album,
        Library
    };
    void calculateReplayGain(RGScanType type);
    void calculateReplayGain(RGScanType type, TrackList tracks, const QString& progressLabel);
    void calculateReplayGainForLibrary();
    void setupReplayGainMenu();
    static QDialog* createRemoveDialog();

    std::shared_ptr<AudioLoader> m_audioLoader;
    MusicLibrary* m_library;
    SettingsManager* m_settings;
    ActionManager* m_actionManager;
    TrackSelectionController* m_selectionController;
};
} // namespace Fooyin::RGScanner
