/*
    SPDX-FileCopyrightText: 2019 Alexander Potashev <aspotashev@gmail.com>

    SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#include <QTest>

#include "base/taskview.h"
#include "export/export.h"
#include "export/exportformats.h"
#include "export/totalsastext.h"
#include "helpers.h"
#include "model/task.h"
#include "model/tasksmodel.h"
#include "widgets/taskswidget.h"

class ExportCSVTest : public QObject
{
    Q_OBJECT

private:
    ReportCriteria createRC(ReportCriteria::REPORTTYPE type);

private Q_SLOTS:
    void testTotalsEmpty();
    void testTotalsSimpleTree();
    void testTimesSimpleTree();
    void testWorkedTimeTreeExport();
    void testSelectedProjectFiltersExports();
    void testXlsxOutputSignature();
    void testPdfOutputSignature();
    // void testHistorySimpleTree();
};

ReportCriteria ExportCSVTest::createRC(ReportCriteria::REPORTTYPE type)
{
    ReportCriteria rc;
    rc.reportType = type;
    rc.from = QDate::currentDate();
    rc.to = QDate::currentDate();
    rc.decimalMinutes = false;
    rc.sessionTimes = false;
    rc.allTasks = true;
    rc.delimiter = QChar::fromLatin1(';');
    rc.quote = QChar::fromLatin1('"');

    return rc;
}

void ExportCSVTest::testTotalsEmpty()
{
    QLocale::setDefault(QLocale(QLocale::C));

    auto *taskView = createTaskView(this, false);
    QVERIFY(taskView);

    const QString &timeString = QLocale().toString(QDateTime::currentDateTime());
    const QString &expected = QStringLiteral(
                                  "Task Totals\n%1\n\n"
                                  "  Time    Task\n----------------------------------------------\nNo tasks.")
                                  .arg(timeString);
    QCOMPARE(totalsAsText(taskView->storage()->tasksModel(), taskView->tasksWidget()->currentItem(), createRC(ReportCriteria::CSVTotalsExport)), expected);
}

void ExportCSVTest::testTotalsSimpleTree()
{
    QLocale::setDefault(QLocale(QLocale::C));

    auto *taskView = createTaskView(this, true);
    QVERIFY(taskView);

    const QString &timeString = QLocale().toString(QDateTime::currentDateTime());
    const QString &expected = QStringLiteral(
                                  "Task Totals\n"
                                  "%1\n"
                                  "\n"
                                  "  Time    Task\n"
                                  "----------------------------------------------\n"
                                  "  0:08    1\n"
                                  "   0:03    2\n"
                                  "  0:07    3\n"
                                  "----------------------------------------------\n"
                                  "  0:15 Total")
                                  .arg(timeString);
    QCOMPARE(totalsAsText(taskView->storage()->tasksModel(), taskView->tasksWidget()->currentItem(), createRC(ReportCriteria::CSVTotalsExport)), expected);
}

void ExportCSVTest::testTimesSimpleTree()
{
    QLocale::setDefault(QLocale(QLocale::C));

    auto *taskView = createTaskView(this, true);
    QVERIFY(taskView);

    const auto &rc = createRC(ReportCriteria::CSVTotalsExport);
    const QUrl &url = createTempFile(this);
    QVERIFY(!url.isEmpty());
    QString output = exportToString(taskView->storage()->projectModel(), taskView->tasksWidget()->currentItem(), rc);
    QCOMPARE(writeExport(output, url), QString());

    const QString &expected = QStringLiteral(
        "\"3\";;0:07;0:07;0:07;0:07\n"
        "\"1\";;0:05;0:05;0:08;0:08\n"
        ";\"2\";0:03;0:03;0:03;0:03\n");
    QCOMPARE(readTextFile(url.toLocalFile()), expected);
}

void ExportCSVTest::testWorkedTimeTreeExport()
{
    QLocale::setDefault(QLocale(QLocale::C));

    auto *taskView = createTaskView(this, true);
    QVERIFY(taskView);

    const auto rc = createRC(ReportCriteria::TreeTotalsExport);
    const QString output = totalsAsTreeText(taskView->storage()->tasksModel(), taskView->tasksWidget()->currentItem(), rc);
    QVERIFY(output.contains(QStringLiteral("Worked Time by Project")));
    QVERIFY(output.contains(QStringLiteral("Project / Project Task")));
    QVERIFY(output.contains(QStringLiteral("\\-- 2")));
    QVERIFY(output.contains(QStringLiteral("0:15  Total")));
}

void ExportCSVTest::testSelectedProjectFiltersExports()
{
    QLocale::setDefault(QLocale(QLocale::C));

    auto *taskView = createTaskView(this, true);
    QVERIFY(taskView);
    auto *tasksModel = taskView->storage()->tasksModel();
    QVERIFY(tasksModel->topLevelItemCount() > 1);

    auto *selectedProject = dynamic_cast<Task *>(tasksModel->topLevelItem(0));
    auto *siblingProject = dynamic_cast<Task *>(tasksModel->topLevelItem(1));
    QVERIFY(selectedProject);
    QVERIFY(siblingProject);

    auto rc = createRC(ReportCriteria::CSVTotalsExport);
    rc.allTasks = false;
    const QString totals = exportToString(taskView->storage()->projectModel(), selectedProject, rc);
    QVERIFY(totals.contains(QStringLiteral("\"%1\"").arg(selectedProject->name())));
    QVERIFY(!totals.contains(QStringLiteral("\"%1\"").arg(siblingProject->name())));

    rc.reportType = ReportCriteria::CSVEventLogExport;
    const QString eventLog = exportToString(taskView->storage()->projectModel(), selectedProject, rc);
    QVERIFY(!eventLog.contains(siblingProject->name()));
}

void ExportCSVTest::testXlsxOutputSignature()
{
    auto rc = createRC(ReportCriteria::CSVTotalsExport);
    rc.delimiter = QStringLiteral(",");
    const QByteArray workbook = exportXlsxToByteArray(QStringLiteral("\"Task\",\"Time\"\n\"A, B\",\"0:30\"\n"), rc);
    QCOMPARE(workbook.left(4), QByteArray("PK\x03\x04", 4));
}

void ExportCSVTest::testPdfOutputSignature()
{
    const QByteArray pdf = exportPdfToByteArray(QStringLiteral("Worked time report\nTask  0:30\n"));
    QCOMPARE(pdf.left(4), QByteArray("%PDF", 4));
}

// void ExportCSVTest::testHistorySimpleTree()
// {
//     QLocale::setDefault(QLocale(QLocale::C));
//
//     auto *taskView = createTaskView(this, true);
//     QVERIFY(taskView);
//
//     const auto &rc = createRC(ReportCriteria::CSVHistoryExport);
//     const QUrl &url = createTempFile(this);
//     QString output = exportToString(taskView->storage()->projectModel(), taskView->tasksWidget()->currentItem(), rc);
//     QCOMPARE(writeExport(output, url), QString());
//
//     const QString &expected = QStringLiteral(
//         "\"Task name\";%1\n"
//         "\"1\";0:05\n"
//         "\"1->2\";0:03\n"
//         "\"3\";0:07\n").arg(QDate::currentDate().toString());
//     QCOMPARE(readTextFile(url.toLocalFile()), expected);
// }

QTEST_MAIN(ExportCSVTest)

#include "exportcsvtest.moc"
