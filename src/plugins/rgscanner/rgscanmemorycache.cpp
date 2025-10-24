/*
 * Fooyin
 *
 * This file is part of Fooyin. It may be used under the terms of the GNU
 * General Public License version 3 or (at your option) any later version.
 *
 * See the COPYING file for more information.
 */

#include "rgscanmemorycache.h"

#include "rgscannerdefs.h"

#include <core/coresettings.h>

#include <QFile>
#include <QMutexLocker>
#include <QString>

#ifdef Q_OS_WIN
#include <windows.h>
#elif defined(Q_OS_LINUX)
#include <sys/sysinfo.h>
#elif defined(Q_OS_DARWIN)
#include <sys/sysctl.h>
#endif

#include <algorithm>

namespace Fooyin::RGScanner {
namespace {
constexpr qint64 MaxCacheBytes = 2LL * 1024 * 1024 * 1024; // 2 GiB safety cap
} // namespace

MemoryCache::MemoryCache()
    : m_limitBytes{0}
    , m_reservedBytes{0}
    , m_enabled{false}
    , m_ratio{0}
{
    reloadSettings();
}

MemoryCache& MemoryCache::instance()
{
    static MemoryCache cache;
    return cache;
}

void MemoryCache::reloadSettings()
{
    const FySettings settings;
    updateConfig(settings.value(MemoryCacheEnabledSetting, false).toBool(),
                 settings.value(MemoryCacheRatioSetting, DefaultMemoryCacheRatio).toInt());
}

void MemoryCache::updateConfig(bool enabled, int ratio)
{
    QMutexLocker locker{&m_mutex};

    m_enabled = enabled;
    if(!m_enabled || ratio <= 0) {
        m_ratio         = 0;
        m_limitBytes    = 0;
        m_reservedBytes = 0;
        return;
    }

    m_ratio = std::clamp(ratio, 1, MaxMemoryCacheRatio);

    const quint64 totalRam = totalSystemMemory();
    qint64 limit           = 0;
    if(totalRam > 0) {
        limit = static_cast<qint64>((static_cast<long double>(totalRam) * m_ratio) / 100.0L);
    }
    else {
        limit = static_cast<qint64>((8LL * 1024 * 1024 * 1024) * m_ratio / 100);
    }

    m_limitBytes    = std::clamp(limit, qint64(0), MaxCacheBytes);
    m_reservedBytes = (std::min)(m_reservedBytes, m_limitBytes);
}

bool MemoryCache::stageFile(const QString& filepath, QByteArray& outData, qint64& reservedSize)
{
    if(!m_enabled || m_limitBytes <= 0) {
        reservedSize = 0;
        return false;
    }

    QFile file{filepath};
    if(!file.open(QIODevice::ReadOnly)) {
        reservedSize = 0;
        return false;
    }

    const qint64 size = file.size();
    if(size <= 0) {
        reservedSize = 0;
        return false;
    }

    if(!tryReserve(size)) {
        reservedSize = 0;
        return false;
    }

    QByteArray data = file.read(size);
    if(data.size() != size) {
        release(size);
        reservedSize = 0;
        return false;
    }

    outData      = std::move(data);
    reservedSize = size;

    return true;
}

void MemoryCache::release(qint64 size)
{
    if(size <= 0) {
        return;
    }

    QMutexLocker locker{&m_mutex};
    m_reservedBytes = std::max<qint64>(0, m_reservedBytes - size);
}

bool MemoryCache::tryReserve(qint64 size)
{
    QMutexLocker locker{&m_mutex};

    if(!m_enabled || m_limitBytes <= 0) {
        return false;
    }
    if(size > m_limitBytes) {
        return false;
    }
    if(m_reservedBytes + size > m_limitBytes) {
        return false;
    }

    m_reservedBytes += size;
    return true;
}

quint64 MemoryCache::totalSystemMemory() const
{
#ifdef Q_OS_WIN
    MEMORYSTATUSEX status;
    status.dwLength = sizeof(status);
    if(GlobalMemoryStatusEx(&status)) {
        return static_cast<quint64>(status.ullTotalPhys);
    }
    return 0;
#elif defined(Q_OS_LINUX)
    struct sysinfo info{};
    if(sysinfo(&info) == 0) {
        return static_cast<quint64>(info.totalram) * info.mem_unit;
    }
    return 0;
#elif defined(Q_OS_DARWIN)
    uint64_t mem{0};
    size_t len = sizeof(mem);
    if(sysctlbyname("hw.memsize", &mem, &len, nullptr, 0) == 0) {
        return static_cast<quint64>(mem);
    }
    return 0;
#else
    return 0;
#endif
}

MemoryScopedReservation::MemoryScopedReservation()
    : m_reservedSize{0}
    , m_active{false}
{ }

MemoryScopedReservation::~MemoryScopedReservation()
{
    release();
}

bool MemoryScopedReservation::load(const QString& filepath)
{
    reset();

    QByteArray stagedData;
    qint64 reserved{0};

    if(!MemoryCache::instance().stageFile(filepath, stagedData, reserved)) {
        return false;
    }

    m_data      = std::move(stagedData);
    auto buffer = std::make_unique<QBuffer>(&m_data);
    if(!buffer->open(QIODevice::ReadOnly)) {
        m_data.clear();
        MemoryCache::instance().release(reserved);
        return false;
    }

    m_buffer       = std::move(buffer);
    m_reservedSize = reserved;
    m_active       = true;

    return true;
}

void MemoryScopedReservation::reset()
{
    release();
}

QIODevice* MemoryScopedReservation::device() const
{
    return m_buffer.get();
}

bool MemoryScopedReservation::active() const
{
    return m_active;
}

void MemoryScopedReservation::release()
{
    if(m_active) {
        MemoryCache::instance().release(m_reservedSize);
        m_active = false;
    }
    m_buffer.reset();
    m_data.clear();
    m_reservedSize = 0;
}

} // namespace Fooyin::RGScanner
