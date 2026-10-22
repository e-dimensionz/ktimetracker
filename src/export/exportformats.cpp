#include "exportformats.h"

#include <QBuffer>
#include <QDateTime>
#include <QFile>
#include <QFontDatabase>
#include <QPageSize>
#include <QPdfWriter>
#include <QRegularExpression>
#include <QTemporaryFile>
#include <QTextDocument>
#include <QVector>

#include <zip.h>

#include <vector>

namespace
{
QVector<QStringList> parseDelimitedText(const QString &data, QChar delimiter, QChar quote)
{
    QVector<QStringList> rows;
    QStringList row;
    QString cell;
    bool quoted = false;

    for (qsizetype index = 0; index < data.size(); ++index) {
        const QChar character = data.at(index);
        if (character == quote) {
            if (quoted && index + 1 < data.size() && data.at(index + 1) == quote) {
                cell.append(quote);
                ++index;
            } else {
                quoted = !quoted;
            }
        } else if (!quoted && character == delimiter) {
            row.append(cell);
            cell.clear();
        } else if (!quoted && (character == QLatin1Char('\n') || character == QLatin1Char('\r'))) {
            row.append(cell);
            rows.append(row);
            row.clear();
            cell.clear();
            if (character == QLatin1Char('\r') && index + 1 < data.size() && data.at(index + 1) == QLatin1Char('\n')) {
                ++index;
            }
        } else {
            cell.append(character);
        }
    }

    if (!cell.isEmpty() || !row.isEmpty()) {
        row.append(cell);
        rows.append(row);
    }
    return rows;
}

QVector<QStringList> spreadsheetRows(const QString &data, const ReportCriteria &criteria)
{
    if (criteria.reportType == ReportCriteria::CSVTotalsExport || criteria.reportType == ReportCriteria::CSVHistoryExport
        || criteria.reportType == ReportCriteria::CSVEventLogExport) {
        const QChar delimiter = criteria.delimiter.isEmpty() ? QLatin1Char(',') : criteria.delimiter.at(0);
        const QChar quote = criteria.quote.isEmpty() ? QLatin1Char('"') : criteria.quote.at(0);
        return parseDelimitedText(data, delimiter, quote);
    }

    QStringList lines = data.split(QLatin1Char('\n'));
    if (!lines.isEmpty() && lines.last().isEmpty()) {
        lines.removeLast();
    }
    QVector<QStringList> rows;
    rows.reserve(lines.size());
    for (const QString &line : std::as_const(lines)) {
        rows.append({line});
    }
    return rows;
}

QString xmlEscaped(QString value)
{
    value.remove(QRegularExpression(QStringLiteral("[\\x00-\\x08\\x0B\\x0C\\x0E-\\x1F]")));
    return value.toHtmlEscaped();
}

QString columnReference(int column)
{
    QString reference;
    do {
        --column;
        reference.prepend(QChar(QLatin1Char('A').unicode() + column % 26));
        column /= 26;
    } while (column > 0);
    return reference;
}

bool addZipEntry(zip_t *archive, std::vector<QByteArray> &contents, const QString &name, const QString &value)
{
    contents.push_back(value.toUtf8());
    const QByteArray path = name.toUtf8();
    zip_source_t *source = zip_source_buffer(archive, contents.back().constData(), contents.back().size(), 0);
    if (!source) {
        return false;
    }
    if (zip_file_add(archive, path.constData(), source, ZIP_FL_ENC_UTF_8) < 0) {
        zip_source_free(source);
        return false;
    }
    return true;
}

QByteArray createWorkbook(const QVector<QStringList> &rows)
{
    QTemporaryFile temporaryFile;
    if (!temporaryFile.open()) {
        return {};
    }
    const QByteArray zipPath = QFile::encodeName(temporaryFile.fileName());
    temporaryFile.close();

    int zipError = 0;
    zip_t *archive = zip_open(zipPath.constData(), ZIP_CREATE | ZIP_TRUNCATE, &zipError);
    if (!archive) {
        return {};
    }

    QString worksheet = QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>")
        + QStringLiteral("<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\"><sheetData>");
    for (qsizetype rowIndex = 0; rowIndex < rows.size(); ++rowIndex) {
        worksheet += QStringLiteral("<row r=\"%1\">").arg(rowIndex + 1);
        const QStringList &columns = rows.at(rowIndex);
        for (qsizetype columnIndex = 0; columnIndex < columns.size(); ++columnIndex) {
            const QString address = columnReference(columnIndex + 1) + QString::number(rowIndex + 1);
            worksheet += QStringLiteral("<c r=\"%1\" t=\"inlineStr\"><is><t xml:space=\"preserve\">%2</t></is></c>")
                             .arg(address, xmlEscaped(columns.at(columnIndex)));
        }
        worksheet += QStringLiteral("</row>");
    }
    worksheet += QStringLiteral("</sheetData></worksheet>");

    const QStringList entries{
        QStringLiteral("[Content_Types].xml"),
        QStringLiteral("_rels/.rels"),
        QStringLiteral("xl/workbook.xml"),
        QStringLiteral("xl/_rels/workbook.xml.rels"),
        QStringLiteral("xl/worksheets/sheet1.xml"),
    };
    const QStringList values{
        QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?><Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\"><Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/><Default Extension=\"xml\" ContentType=\"application/xml\"/><Override PartName=\"/xl/workbook.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml\"/><Override PartName=\"/xl/worksheets/sheet1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml\"/></Types>"),
        QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?><Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\"><Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"xl/workbook.xml\"/></Relationships>"),
        QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?><workbook xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\"><sheets><sheet name=\"KTimeTracker\" sheetId=\"1\" r:id=\"rId1\"/></sheets></workbook>"),
        QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?><Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\"><Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet\" Target=\"worksheets/sheet1.xml\"/></Relationships>"),
        worksheet,
    };

    std::vector<QByteArray> contents;
    contents.reserve(entries.size());
    bool success = true;
    for (qsizetype index = 0; index < entries.size(); ++index) {
        success = addZipEntry(archive, contents, entries.at(index), values.at(index)) && success;
    }
    if (!success || zip_close(archive) < 0) {
        zip_discard(archive);
        return {};
    }

    QFile resultFile(temporaryFile.fileName());
    if (!resultFile.open(QIODevice::ReadOnly)) {
        return {};
    }
    return resultFile.readAll();
}
}

QByteArray exportXlsxToByteArray(const QString &data, const ReportCriteria &criteria)
{
    return createWorkbook(spreadsheetRows(data, criteria));
}

QByteArray exportPdfToByteArray(const QString &data)
{
    QByteArray bytes;
    QBuffer buffer(&bytes);
    if (!buffer.open(QIODevice::WriteOnly)) {
        return {};
    }

    QPdfWriter writer(&buffer);
    writer.setPageSize(QPageSize(QPageSize::A4));
    QTextDocument document;
    document.setDefaultFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    document.setPlainText(data);
    document.setPageSize(writer.pageLayout().paintRect(QPageLayout::Point).size());
    document.print(&writer);
    buffer.close();
    return bytes;
}