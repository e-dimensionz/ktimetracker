/*
    SPDX-FileCopyrightText: 2019 Alexander Potashev <aspotashev@gmail.com>

    SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#include "export.h"

#include <KIO/StoredTransferJob>
#include <KLocalizedString>
#include <QFile>
#include <QSaveFile>
#include <QUrl>

#include "csveventlog.h"
#include "csvhistory.h"
#include "csvtotals.h"
#include "exportformats.h"
#include "ktt_debug.h"
#include "model/projectmodel.h"
#include "totalsastext.h"

QString exportToString(ProjectModel *model, Task *currentTask, const ReportCriteria &rc)
{
    switch (rc.reportType) {
    case ReportCriteria::CSVTotalsExport:
        return exportCSVToString(model->tasksModel(), currentTask, rc);
    case ReportCriteria::CSVHistoryExport:
        return exportCSVHistoryToString(model, currentTask, rc);
    case ReportCriteria::CSVEventLogExport:
        return exportCSVEventLogToString(model, currentTask, rc);
    case ReportCriteria::TextTotalsExport:
        return totalsAsText(model->tasksModel(), currentTask, rc);
    case ReportCriteria::TreeTotalsExport:
        return totalsAsTreeText(model->tasksModel(), currentTask, rc);
    default:
        return QString();
    }
}

QString writeExport(const QString &data, const QUrl &url)
{
    const QByteArray bytes = data.toUtf8();
    QString err;

    // store the file locally or remote
    if (url.isLocalFile()) {
        qCDebug(KTT_LOG) << "storing a local file";
        QString filename = url.toLocalFile();
        if (filename.isEmpty()) {
            filename = url.url();
        }

        QSaveFile file(filename);
        if (!file.open(QIODevice::WriteOnly)) {
            err = i18n("Could not open \"%1\".", filename);
            qCDebug(KTT_LOG) << "Could not open file";
        }
        if (err.isEmpty()) {
            if (file.write(bytes) != bytes.size() || !file.commit()) {
                err = i18n("Could not write to \"%1\".", filename);
            }
        }
    } else {
        // use remote file
        auto *const job = KIO::storedPut(bytes, url, -1);
        // KJobWidgets::setWindow(job, &dialog); // TODO: add progress dialog
        if (!job->exec()) {
            err = QString::fromLatin1("Could not upload");
        }
    }

    return err;
}

QString writeExport(const QString &data, const QUrl &url, const ReportCriteria &criteria)
{
    const QString fileName = url.fileName();
    QByteArray bytes;
    if (fileName.endsWith(QStringLiteral(".xlsx"), Qt::CaseInsensitive)) {
        bytes = exportXlsxToByteArray(data, criteria);
    } else if (fileName.endsWith(QStringLiteral(".pdf"), Qt::CaseInsensitive)) {
        bytes = exportPdfToByteArray(data);
    } else {
        bytes = data.toUtf8();
    }
    if (bytes.isEmpty() && !data.isEmpty()) {
        return i18n("Could not create the requested export format.");
    }

    QString error;
    if (url.isLocalFile()) {
        const QString filename = url.toLocalFile();
        QSaveFile file(filename);
        if (!file.open(QIODevice::WriteOnly)) {
            return i18n("Could not open \"%1\".", filename);
        }
        if (file.write(bytes) != bytes.size() || !file.commit()) {
            error = i18n("Could not write to \"%1\".", filename);
        }
    } else {
        auto *const job = KIO::storedPut(bytes, url, -1);
        if (!job->exec()) {
            error = i18n("Could not upload export.");
        }
    }
    return error;
}
