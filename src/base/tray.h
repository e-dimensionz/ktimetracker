/*
    SPDX-FileCopyrightText: 2003 Scott Monachello <smonach@cox.net>
    SPDX-FileCopyrightText: 2019 Alexander Potashev <aspotashev@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef TRAY_ICON_H
#define TRAY_ICON_H

#include <QList>

#include <KStatusNotifierItem>

QT_BEGIN_NAMESPACE
class QAction;
class QMenu;
class QMovie;
QT_END_NAMESPACE

class MainWindow;
class Task;
class TimeTrackerWidget;

class TrayIcon : public KStatusNotifierItem
{
    Q_OBJECT

public:
    explicit TrayIcon(MainWindow *parent);
    ~TrayIcon() override = default;

public Q_SLOTS:
    void activate(const QPoint &pos = QPoint()) override;

private:
    void refreshRecentTasks();
    void toggleTask(const QString &taskId);

    QMovie *m_animation;
    MainWindow *m_mainWindow;
    TimeTrackerWidget *m_widget;
    QAction *m_recentSectionAction;
    QAction *m_noRecentTasksAction;
    QList<QAction *> m_recentTaskActions;

public Q_SLOTS:
    void startClock();
    void stopClock();
    void updateToolTip(const QList<Task *> &activeTasks);

protected Q_SLOTS:
    void setActiveIcon(int frame);
};

#endif // TRAY_ICON_H
