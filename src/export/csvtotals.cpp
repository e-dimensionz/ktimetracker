/*
    SPDX-FileCopyrightText: 2003 Scott Monachello <smonach@cox.net>
    SPDX-FileCopyrightText: 2019 Alexander Potashev <aspotashev@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "csvtotals.h"

#include <QApplication>

#include "base/ktimetrackerutility.h"
#include "model/task.h"
#include "model/tasksmodel.h"

QString exportCSVToString(TasksModel *tasksModel, Task *currentTask, const ReportCriteria &rc)
{
    QString delim = rc.delimiter;
    QString dquote = rc.quote;
    QString double_dquote = dquote + dquote;

    QString retval;

    const QList<Task *> tasks = rc.allTasks ? tasksModel->getAllTasks() : (currentTask ? currentTask->selfAndDescendants() : QList<Task *>{});
    const int depthOffset = rc.allTasks || !currentTask ? 0 : currentTask->depth();

    // Find max task depth
    int maxdepth = 0;
    for (Task *task : tasks) {
        //        if (dialog.wasCanceled()) {
        //            break;
        //        }
        //
        //        dialog.setValue(dialog.value() + 1);

        //        if (tasknr % 15 == 0) {
        //            QApplication::processEvents(); // repainting is slow
        //        }
        QApplication::processEvents();

        if (task->depth() - depthOffset > maxdepth) {
            maxdepth = task->depth() - depthOffset;
        }
    }

    // Export to file
    for (Task *task : tasks) {
        //        if (dialog.wasCanceled()) {
        //            break;
        //        }
        //
        //        dialog.setValue(dialog.value() + 1);

        //        if (tasknr % 15 == 0) {
        //            QApplication::processEvents(); // repainting is slow
        //        }
        QApplication::processEvents();

        // indent the task in the csv-file:
        const int relativeDepth = task->depth() - depthOffset;
        for (int i = 0; i < relativeDepth; ++i) {
            retval += delim;
        }

        /*
        // CSV compliance
        // Surround the field with quotes if the field contains
        // a comma (delim) or a double quote
        if (task->name().contains(delim) || task->name().contains(dquote))
        to_quote = true;
        else
        to_quote = false;
        */
        bool to_quote = true;

        if (to_quote) {
            retval += dquote;
        }

        // Double quotes replaced by a pair of consecutive double quotes
        retval += task->name().replace(dquote, double_dquote);

        if (to_quote) {
            retval += dquote;
        }

        // maybe other tasks are more indented, so to align the columns:
        for (int i = 0; i < maxdepth - relativeDepth; ++i) {
            retval += delim;
        }

        retval += delim + formatTime(static_cast<double>(task->sessionTime()), rc.decimalMinutes) + delim
            + formatTime(static_cast<double>(task->time()), rc.decimalMinutes) + delim
            + formatTime(static_cast<double>(task->totalSessionTime()), rc.decimalMinutes) + delim
            + formatTime(static_cast<double>(task->totalTime()), rc.decimalMinutes) + QStringLiteral("\n");
    }

    return retval;
}
