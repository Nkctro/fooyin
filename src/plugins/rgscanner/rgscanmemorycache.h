/*
 * Fooyin
 *
 * This file is part of Fooyin. It may be used under the terms of the GNU
 * General Public License version 3 or (at your option) any later version.
 *
 * See the COPYING file for more information.
 */

#pragma once

#include <QByteArray>
#include <QBuffer>
#include <QMutex>
#include <QString>

#include <memory>

class QIODevice;

namespace Fooyin::RGScanner {

class MemoryCache
{
public:
    static MemoryCache& instance();

    bool stageFile(const QString& filepath, QByteArray& outData, qint64& reservedSize);
    void release(qint64 size);
    void reloadSettings();
    void updateConfig(bool enabled, int ratio);

private:
    MemoryCache();

    bool tryReserve(qint64 size);
    quint64 totalSystemMemory() const;

    mutable QMutex m_mutex;
    qint64 m_limitBytes;
    qint64 m_reservedBytes;
    bool m_enabled;
    int m_ratio;
};

class MemoryScopedReservation
{
public:
    MemoryScopedReservation();
    ~MemoryScopedReservation();

    bool load(const QString& filepath);
    void reset();
    QIODevice* device() const;
    bool active() const;

private:
    void release();

    QByteArray m_data;
    std::unique_ptr<QBuffer> m_buffer;
    qint64 m_reservedSize;
    bool m_active;
};

} // namespace Fooyin::RGScanner
