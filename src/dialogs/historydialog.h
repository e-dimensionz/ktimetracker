/*
    SPDX-FileCopyrightText: 2009 Thorsten Staerk and Mathias Soeken <msoeken@tzi.de>
    SPDX-FileCopyrightText: 2019 Alexander Potashev <aspotashev@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef HISTORYDIALOG_H
#define HISTORYDIALOG_H

#include <QDialog>

#include "ui_historydialog.h"

class ActivityTimelineWidget;
class Event;
class ProjectModel;
class Task;

class HistoryDialog : public QDialog
{
    Q_OBJECT

public:
    explicit HistoryDialog(QWidget *parent, ProjectModel *projectModel, Task *scopeTask = nullptr);
    ~HistoryDialog() override = default;

    static const QString dateTimeFormat;

protected:
    void changeEvent(QEvent *e) override;

private Q_SLOTS:
    /**
     * The historywidget contains all events and can be shown with File | Edit History.
     * The user can change dates and comments in there.
     * A change triggers this procedure, it shall store the new values in the calendar.
     */
    void onCellChanged(int row, int col);

    void onDeleteClicked();
    void on_buttonbox_rejected();
    void updateTimelineView();

private:
    QString listAllEvents();
    void refresh();
    void updateScopeSummary();

    ProjectModel *m_projectModel;
    Task *m_scopeTask;
    QList<Event *> m_visibleEvents;
    ActivityTimelineWidget *m_timelineWidget;
    Ui::HistoryDialog m_ui;
};

#endif // HISTORYDIALOG_H
