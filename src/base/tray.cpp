/*
    SPDX-FileCopyrightText: 2003 Scott Monachello <smonach@cox.net>
    SPDX-FileCopyrightText: 2019 Alexander Potashev <aspotashev@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
 * TrayIcon.
 *
 * This implements the functionality of the little icon in the kpanel
 * tray. Among which are tool tips and the running clock animated icon
 */

#include "tray.h"

#include <algorithm>

#include <QAction>
#include <QCursor>
#include <QHash>
#include <QIcon>
#include <QMenu>
#include <QMovie>
#include <QScreen>
#include <QToolTip>

#include <KLocalizedString>
#include <kstatusnotifieritem_version.h>

#include "base/ktimetrackerutility.h"
#include "mainwindow.h"
#include "model/event.h"
#include "model/eventsmodel.h"
#include "model/task.h"
#include "model/tasksmodel.h"
#include "taskview.h"
#include "timetrackerstorage.h"
#include "timetrackerwidget.h"

namespace
{
bool isEffectivelyComplete(Task *task)
{
    if (task->isComplete()) {
        return true;
    }

    bool hasTaskChildren = false;
    for (int index = 0; index < task->childCount(); ++index) {
        auto *childTask = dynamic_cast<Task *>(task->child(index));
        if (childTask) {
            hasTaskChildren = true;
            if (!isEffectivelyComplete(childTask)) {
                return false;
            }
        }
    }

    return hasTaskChildren;
}

}

TrayIcon::TrayIcon(MainWindow *parent)
    : KStatusNotifierItem(parent)
    , m_mainWindow(parent)
    , m_widget(qobject_cast<TimeTrackerWidget *>(parent->centralWidget()))
    , m_recentSectionAction(nullptr)
    , m_noRecentTasksAction(nullptr)
{
    Q_INIT_RESOURCE(pics);

    setObjectName(QStringLiteral("Ktimetracker Tray"));

    m_animation = new QMovie(QStringLiteral(":/pics/active-icon.gif"), QByteArray("GIF"), this);
    connect(m_animation, &QMovie::frameChanged, this, &TrayIcon::setActiveIcon);
    m_animation->jumpToFrame(0);
    setIconByPixmap(QIcon(QStringLiteral(":/pics/watch.gif")));
    setStatus(KStatusNotifierItem::Active);

    setStandardActionsEnabled(false);
#if KSTATUSNOTIFIERITEM_VERSION >= ((6 << 16) | (14 << 8))
    setIsMenu(true);
#endif

    auto *menu = KStatusNotifierItem::contextMenu();

    if (m_widget) {
        QAction *configureAction = m_widget->action(QStringLiteral("configure_ktimetracker"));
        if (configureAction) {
            menu->addAction(configureAction);
        }

        QAction *stopAllAction = m_widget->action(QStringLiteral("stopAll"));
        if (stopAllAction) {
            menu->addAction(stopAllAction);
        }

        m_recentSectionAction = menu->addSection(i18nc("tray menu section", "Recently tracked"));
        m_noRecentTasksAction = new QAction(i18n("No tracked tasks yet"), this);
        m_noRecentTasksAction->setEnabled(false);
        menu->addAction(m_noRecentTasksAction);
        for (int index = 0; index < 3; ++index) {
            auto *action = new QAction(this);
            action->setCheckable(true);
            action->setVisible(false);
            connect(action, &QAction::triggered, this, [this, action] {
                toggleTask(action->property("taskId").toString());
            });
            menu->addAction(action);
            m_recentTaskActions.append(action);
        }

        connect(menu, &QMenu::aboutToShow, this, &TrayIcon::refreshRecentTasks);
        connect(m_widget, &TimeTrackerWidget::tasksChanged, this, &TrayIcon::refreshRecentTasks);
        connect(m_widget, &TimeTrackerWidget::minutesUpdated, this, &TrayIcon::refreshRecentTasks);
    }

    menu->addSeparator();

    auto *minimizeAction = new QAction(i18nc("tray menu action", "Minimize"), this);
    connect(minimizeAction, &QAction::triggered, m_mainWindow, &QWidget::showMinimized);
    menu->addAction(minimizeAction);

    auto *quitAction = new QAction(i18nc("tray menu action", "Quit"), this);
    connect(quitAction, &QAction::triggered, m_mainWindow, &MainWindow::quit);
    menu->addAction(quitAction);

    updateToolTip(QList<Task *>());
}

void TrayIcon::activate(const QPoint &pos)
{
#if KSTATUSNOTIFIERITEM_VERSION >= ((6 << 16) | (14 << 8))
    Q_UNUSED(pos)
#else
    refreshRecentTasks();
    KStatusNotifierItem::contextMenu()->popup(pos.isNull() ? QCursor::pos() : pos);
#endif
}

void TrayIcon::toggleTask(const QString &taskId)
{
    if (!m_widget || taskId.isEmpty()) {
        return;
    }

    if (m_widget->isActive(taskId)) {
        m_widget->stopTimerFor(taskId);
    } else {
        m_widget->startTimerFor(taskId);
    }
}

void TrayIcon::refreshRecentTasks()
{
    QList<Task *> recentTasks;
    QHash<QString, QDateTime> latestStartByTask;
    TaskView *taskView = m_widget ? m_widget->currentTaskView() : nullptr;
    if (taskView && taskView->storage()->isLoaded()) {
        auto *storage = taskView->storage();
        for (Event *event : storage->eventsModel()->events()) {
            const QString taskId = event->relatedTo();
            const QDateTime start = event->dtStart();
            if (!taskId.isEmpty() && (!latestStartByTask.contains(taskId) || start > latestStartByTask.value(taskId))) {
                latestStartByTask.insert(taskId, start);
            }
        }

        for (Task *task : storage->tasksModel()->getAllTasks()) {
            if (latestStartByTask.contains(task->uid()) && !isEffectivelyComplete(task)) {
                recentTasks.append(task);
            }
        }

        std::sort(recentTasks.begin(), recentTasks.end(), [&latestStartByTask](Task *left, Task *right) {
            return latestStartByTask.value(left->uid()) > latestStartByTask.value(right->uid());
        });
    }

    if (m_noRecentTasksAction) {
        m_noRecentTasksAction->setVisible(recentTasks.isEmpty());
    }
    if (m_recentSectionAction) {
        m_recentSectionAction->setVisible(!recentTasks.isEmpty());
    }

    for (qsizetype index = 0; index < m_recentTaskActions.size(); ++index) {
        QAction *action = m_recentTaskActions.at(index);
        const bool hasTask = index < recentTasks.size();
        action->setVisible(hasTask);
        if (!hasTask) {
            continue;
        }

        Task *task = recentTasks.at(index);
        const bool running = task->isRunning();
        action->setProperty("taskId", task->uid());
        const QString actionText = i18n("%1 (Total: %2)", task->name(), formatTime(task->totalTime()));
        action->setText(actionText);
        action->setIcon(QIcon::fromTheme(running ? QStringLiteral("media-playback-stop") : QStringLiteral("media-playback-start")));
        action->setChecked(running);
    }
}

void TrayIcon::startClock()
{
    setActiveIcon(m_animation->currentFrameNumber());
    m_animation->start();
}

void TrayIcon::stopClock()
{
    m_animation->stop();
    setIconByPixmap(QIcon(QStringLiteral(":/pics/watch.gif")));
}

void TrayIcon::setActiveIcon(int /*unused*/)
{
    setIconByPixmap(QIcon(m_animation->currentPixmap()));
}

void TrayIcon::updateToolTip(const QList<Task *> &activeTasks)
{
    if (activeTasks.isEmpty()) {
        this->setToolTip(QStringLiteral("ktimetracker"), QStringLiteral("ktimetracker"), i18n("No active tasks"));
        return;
    }

    QFontMetrics fm(QToolTip::font());
    const QString continued = i18nc("ellipsis to truncate long list of tasks", ", ...");
    const int buffer = fm.boundingRect(continued).width();
    const int desktopWidth = associatedWindow()->screen()->geometry().width();
    const int maxWidth = desktopWidth - buffer;

    QString qTip;
    QString s;

    // Build the tool tip with all of the names of the active tasks.
    // If at any time the width of the tool tip is larger than the desktop,
    // stop building it.

    for (int i = 0; i < activeTasks.count(); ++i) {
        Task *task = activeTasks.at(i);
        if (i > 0) {
            s += i18nc("separator between task names", ", ") + task->name();
        } else {
            s += task->name();
        }

        int width = fm.boundingRect(s).width();
        if (width > maxWidth) {
            qTip += continued;
            break;
        }
        qTip = s;
    }
    this->setToolTip(QStringLiteral("ktimetracker"), QStringLiteral("ktimetracker"), qTip);
}

#include "moc_tray.cpp"
