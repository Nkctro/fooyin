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

#include "logmodel.h"

#include <utils/enum.h>

#include <QApplication>
#include <QIcon>
#include <QRegularExpression>
#include <QStyle>

using namespace Qt::StringLiterals;

constexpr auto MessageSplit = R"lit(([^:]+): (.+))lit";

namespace {
QIcon iconForType(QtMsgType type)
{
    // Cache icons per style instance to avoid repeated standardIcon lookups on hot paths.
    static QStyle* cachedStyle = nullptr;
    static QIcon infoIcon;
    static QIcon warningIcon;
    static QIcon criticalIcon;

    if(QStyle* const currentStyle = QApplication::style()) {
        if(currentStyle != cachedStyle || infoIcon.isNull() || warningIcon.isNull() || criticalIcon.isNull()) {
            cachedStyle  = currentStyle;
            infoIcon     = currentStyle->standardIcon(QStyle::SP_MessageBoxInformation);
            warningIcon  = currentStyle->standardIcon(QStyle::SP_MessageBoxWarning);
            criticalIcon = currentStyle->standardIcon(QStyle::SP_MessageBoxCritical);
        }
    }
    else {
        cachedStyle = nullptr;
    }

    switch(type) {
        case(QtDebugMsg):
        case(QtInfoMsg):
            return infoIcon;
        case(QtWarningMsg):
            return warningIcon;
        case(QtCriticalMsg):
        case(QtFatalMsg):
            return criticalIcon;
        default:
            return {};
    }
}
} // namespace

namespace Fooyin {
LogModel::LogModel(QObject* parent)
    : QAbstractListModel{parent}
    , m_maxEntries{0}
{ }

void LogModel::addEntry(ConsoleEntry entry)
{
    if(m_maxEntries > 0 && std::cmp_greater_equal(m_items.size(), m_maxEntries)) {
        beginRemoveRows({}, 0, 0);
        m_items.pop_front();
        endRemoveRows();
    }

    static const QRegularExpression regex(QLatin1String{MessageSplit});
    const QRegularExpressionMatch match = regex.match(entry.message);
    if(match.hasMatch()) {
        entry.category = match.captured(1);
        entry.message  = match.captured(2);
    }

    const int row = rowCount({});
    beginInsertRows({}, row, row);
    m_items.push_back(std::move(entry));
    endInsertRows();
}

void LogModel::addEntries(std::vector<ConsoleEntry> entries)
{
    if(entries.empty()) {
        return;
    }

    static const QRegularExpression regex(QLatin1String{MessageSplit});

    for(auto& entry : entries) {
        const QRegularExpressionMatch match = regex.match(entry.message);
        if(match.hasMatch()) {
            entry.category = match.captured(1);
            entry.message  = match.captured(2);
        }
    }

    const int availableSpace = (m_maxEntries > 0) ? std::max<int>(0, m_maxEntries - static_cast<int>(m_items.size()))
                                                  : static_cast<int>(entries.size());

    if(m_maxEntries > 0 && std::cmp_greater(entries.size(), availableSpace)) {
        const int excess = static_cast<int>(entries.size()) - availableSpace;
        entries.erase(entries.begin(), entries.begin() + excess);
    }

    const int totalAfterInsert = static_cast<int>(m_items.size() + entries.size());
    if(m_maxEntries > 0 && totalAfterInsert > m_maxEntries) {
        const int toRemove = totalAfterInsert - m_maxEntries;
        beginRemoveRows({}, 0, toRemove - 1);
        m_items.erase(m_items.begin(), m_items.begin() + toRemove);
        endRemoveRows();
    }

    const int start = rowCount({});
    const int end   = start + static_cast<int>(entries.size()) - 1;

    beginInsertRows({}, start, end);
    for(auto& entry : entries) {
        m_items.push_back(std::move(entry));
    }
    endInsertRows();
}

void LogModel::setMaxEntries(int maxEntries)
{
    m_maxEntries = maxEntries;

    if(m_maxEntries > 0 && std::cmp_greater_equal(m_items.size(), m_maxEntries)) {
        const int offset = static_cast<int>(m_items.size()) - m_maxEntries;
        beginRemoveRows({}, 0, offset - 1);
        m_items.erase(m_items.begin(), m_items.begin() + offset);
        endRemoveRows();
    }
}

int LogModel::getMaxEntries() const
{
    return m_maxEntries;
}

QString LogModel::typeToString(QtMsgType type)
{
    switch(type) {
        case(QtDebugMsg):
            return u"Debug"_s;
        case(QtInfoMsg):
            return u"Info"_s;
        case(QtWarningMsg):
            return u"Warning"_s;
        case(QtCriticalMsg):
            return u"Critical"_s;
        case(QtFatalMsg):
            return u"Fatal"_s;
        default:
            return {};
    }
}

void LogModel::clear()
{
    beginResetModel();
    m_items.clear();
    endResetModel();
}

std::deque<ConsoleEntry> LogModel::entries()
{
    return m_items;
}

QVariant LogModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if(role == Qt::TextAlignmentRole) {
        return (Qt::AlignHCenter);
    }

    if(role != Qt::DisplayRole || orientation == Qt::Orientation::Vertical) {
        return {};
    }

    const auto column = static_cast<Column>(section);
    return Utils::Enum::toString(column);
}

QVariant LogModel::data(const QModelIndex& index, int role) const
{
    if(!checkIndex(index, CheckIndexOption::IndexIsValid)) {
        return {};
    }

    const int row     = index.row();
    const auto column = static_cast<Column>(index.column());

    auto const& item = m_items.at(row);

    switch(role) {
        case(Qt::DisplayRole): {
            switch(column) {
                case(Level):
                    return typeToString(item.type);
                case(Time):
                    return item.time;
                case(Message):
                    return item.message;
                default:
                    break;
            }
            break;
        }
        case(Qt::DecorationRole): {
            if(column == Level) {
                return iconForType(item.type);
            }
            break;
        }
        default:
            break;
    }

    return {};
}

int LogModel::columnCount(const QModelIndex& /*parent*/) const
{
    return 3;
}

int LogModel::rowCount(const QModelIndex& /*parent*/) const
{
    return static_cast<int>(m_items.size());
}

} // namespace Fooyin
