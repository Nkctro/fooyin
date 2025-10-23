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

#include "rgscanresults.h"

#include "rgscanresultsmodel.h"

#include <core/library/musiclibrary.h>
#include <utils/stringutils.h>

#include <QDialogButtonBox>
#include <QGridLayout>
#include <QHeaderView>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QTableView>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace Fooyin::RGScanner {
RGScanResults::RGScanResults(MusicLibrary* library, TrackList tracks, std::chrono::milliseconds timeTaken,
                             QWidget* parent)
    : QDialog{parent}
    , m_library{library}
    , m_tracks{std::move(tracks)}
    , m_resultsView{new QTableView(this)}
    , m_resultsModel{new RGScanResultsModel(m_tracks, this)}
    , m_status{new QLabel(tr("Time taken") + ": "_L1 + Utils::msToString(timeTaken, false), this)}
    , m_buttonBox{new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this)}
    , m_progress{new QProgressBar(this)}
{
    setWindowTitle(tr("ReplayGain Scan Results"));
    setModal(true);

    m_resultsView->setModel(m_resultsModel);
    m_resultsView->verticalHeader()->hide();
    m_resultsView->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_resultsView->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_resultsView->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_resultsView->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_resultsView->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);

    auto* applyButton = m_buttonBox->button(QDialogButtonBox::Ok);
    applyButton->setText(tr("&Update File Tags"));
    QObject::connect(m_buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    QObject::connect(m_buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    m_progress->setRange(0, 0);
    m_progress->setTextVisible(true);
    m_progress->setFormat(tr("%p%"));
    m_progress->setVisible(false);

    auto* layout = new QGridLayout(this);
    layout->addWidget(m_resultsView, 0, 0, 1, 2);
    layout->addWidget(m_status, 1, 0, 1, 2);
    layout->addWidget(m_progress, 2, 0, 1, 2);
    layout->addWidget(m_buttonBox, 3, 0, 1, 2);
    layout->setRowStretch(0, 1);
    layout->setColumnStretch(0, 1);
}

void RGScanResults::accept()
{
    if(m_tracks.empty()) {
        QDialog::accept();
        return;
    }

    if(m_writeProgressConnection) {
        QObject::disconnect(m_writeProgressConnection);
        m_writeProgressConnection = {};
    }

    const int total = static_cast<int>(m_tracks.size());
    m_status->setText(tr("Writing ReplayGain info…"));
    m_progress->setVisible(true);
    m_progress->setRange(0, total);
    m_progress->setValue(0);

    auto* okButton = m_buttonBox->button(QDialogButtonBox::Ok);
    okButton->setEnabled(false);

    m_writeProgressConnection = QObject::connect(
        m_library, &MusicLibrary::tracksWriteProgress, this,
        [this, total, okButton](int current, int totalCount, const QString& filepath) {
            const int maximum = totalCount > 0 ? totalCount : total;
            if(m_progress->maximum() != maximum) {
                m_progress->setMaximum(maximum);
            }
            const int clampedValue = std::clamp(current, 0, maximum);
            m_progress->setValue(clampedValue);
            if(clampedValue < maximum && !filepath.isEmpty()) {
                m_status->setText(tr("Writing: %1").arg(filepath));
            }
            else if(clampedValue >= maximum && maximum > 0) {
                m_status->setText(tr("Write finished"));
            }
            if(clampedValue >= maximum && maximum > 0) {
                okButton->setEnabled(true);
            }
        });

    QObject::connect(
        m_library, &MusicLibrary::tracksMetadataChanged, this,
        [this]() {
            if(m_writeProgressConnection) {
                QObject::disconnect(m_writeProgressConnection);
                m_writeProgressConnection = {};
            }
            if(m_progress->isVisible()) {
                m_progress->setValue(m_progress->maximum());
            }
            QDialog::accept();
        },
        Qt::SingleShotConnection);

    const auto request = m_library->writeTrackMetadata(m_tracks);
    QObject::connect(
        m_buttonBox, &QDialogButtonBox::rejected, this,
        [this, request]() {
            request.cancel();
            if(m_writeProgressConnection) {
                QObject::disconnect(m_writeProgressConnection);
                m_writeProgressConnection = {};
            }
            m_status->setText(tr("Write cancelled"));
            m_progress->setVisible(false);
            m_buttonBox->button(QDialogButtonBox::Ok)->setEnabled(true);
        },
        Qt::SingleShotConnection);
}

QSize RGScanResults::sizeHint() const
{
    QSize size = m_resultsView->sizeHint();
    size.rheight() += 200;
    size.rwidth() += 400;
    return size;
}

QSize RGScanResults::minimumSizeHint() const
{
    return QDialog::minimumSizeHint();
}
} // namespace Fooyin::RGScanner

