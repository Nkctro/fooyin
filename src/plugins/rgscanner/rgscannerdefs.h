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

#include <algorithm>
#include <atomic>

#include <QThread>
#include <core/coresettings.h>

namespace Fooyin::RGScanner {
constexpr auto ScannerPage = "Fooyin.Page.Playback.ReplayGain.Calculating";

constexpr auto ScannerOption             = "RGScanner/Scanner";
constexpr auto TruePeakSetting           = "RGScanner/TruePeak";
constexpr auto AlbumGroupScriptSetting   = "RGScanner/AlbumGroupScript";
constexpr auto DefaultAlbumGroupScript   = "%albumartist% - %date% - %album%";
constexpr auto ThreadLimitSetting        = "RGScanner/ThreadLimit";
constexpr auto DefaultThreadLimit        = 4;
constexpr auto MaxThreadLimit            = 32;
constexpr auto MemoryCacheEnabledSetting = "RGScanner/MemoryCacheEnabled";
constexpr auto MemoryCacheRatioSetting   = "RGScanner/MemoryCacheRatio";
constexpr auto DefaultMemoryCacheRatio   = 15;
constexpr auto MaxMemoryCacheRatio       = 90;

inline std::atomic<int> g_threadLimitCache{-1};

inline int defaultThreadLimit()
{
    int ideal = QThread::idealThreadCount();
    if(ideal <= 0) {
        ideal = 4;
    }
    return std::clamp(ideal * 4, 4, MaxThreadLimit);
}

inline int currentThreadLimit()
{
    int cached = g_threadLimitCache.load(std::memory_order_acquire);
    if(cached >= 1) {
        return cached;
    }

    Fooyin::FySettings settings;
    int configured = settings.value(ThreadLimitSetting, 0).toInt();
    if(configured <= 0) {
        configured = defaultThreadLimit();
    }
    configured = std::clamp(configured, 1, MaxThreadLimit);
    g_threadLimitCache.store(configured, std::memory_order_release);
    return configured;
}

inline void setThreadLimit(int value)
{
    value = std::clamp(value, 1, MaxThreadLimit);
    g_threadLimitCache.store(value, std::memory_order_release);
}

inline void invalidateThreadLimit()
{
    g_threadLimitCache.store(-1, std::memory_order_release);
}
} // namespace Fooyin::RGScanner
