#ifndef KTIMETRACKER_EXPORTFORMATS_H
#define KTIMETRACKER_EXPORTFORMATS_H

#include <QByteArray>
#include <QString>

#include "base/reportcriteria.h"

QByteArray exportXlsxToByteArray(const QString &data, const ReportCriteria &criteria);
QByteArray exportPdfToByteArray(const QString &data);

#endif // KTIMETRACKER_EXPORTFORMATS_H