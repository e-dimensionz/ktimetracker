/*
    SPDX-FileCopyrightText: 2007 Thorsten Staerk and Mathias Soeken <msoeken@tzi.de>
    SPDX-FileCopyrightText: 2019 Alexander Potashev <aspotashev@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "historydialog.h"

#include <algorithm>
#include <QHash>
#include <QDate>
#include <QDateTimeEdit>
#include <QFontMetrics>
#include <QHeaderView>
#include <QItemDelegate>
#include <QLabel>
#include <QLocale>
#include <QPainter>
#include <QPaintEvent>
#include <QPushButton>
#include <QSet>
#include <QTimer>
#include <QVBoxLayout>

#include <KMessageBox>
#include <KStandardGuiItem>

#include "base/ktimetrackerutility.h"
#include "ktt_debug.h"
#include "model/event.h"
#include "model/eventsmodel.h"
#include "model/projectmodel.h"
#include "model/task.h"
#include "model/tasksmodel.h"

const QString HistoryDialog::dateTimeFormat = QStringLiteral("yyyy-MM-dd HH:mm:ss");

class HistoryWidgetDelegate : public QItemDelegate
{
public:
    explicit HistoryWidgetDelegate(QObject *parent)
        : QItemDelegate(parent)
    {
    }

    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex & /*index*/) const override
    {
        auto *editor = new QDateTimeEdit(parent);
        editor->setAutoFillBackground(true);
        editor->setPalette(option.palette);
        editor->setBackgroundRole(QPalette::Window);
        editor->setCalendarPopup(true);
        editor->setDisplayFormat(HistoryDialog::dateTimeFormat);
        return editor;
    }

    void setEditorData(QWidget *editor, const QModelIndex &index) const override
    {
        QDateTime dateTime = QDateTime::fromString(index.model()->data(index, Qt::DisplayRole).toString(), HistoryDialog::dateTimeFormat);
        auto *dateTimeWidget = qobject_cast<QDateTimeEdit *>(editor);
        if (dateTimeWidget) {
            dateTimeWidget->setDateTime(dateTime);
            dateTimeWidget->setDisplayFormat(HistoryDialog::dateTimeFormat);
        } else {
            qCWarning(KTT_LOG) << "Cast to QDateTimeEdit failed";
        }
    }

    void setModelData(QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const override
    {
        auto *dateTimeWidget = qobject_cast<QDateTimeEdit *>(editor);
        if (dateTimeWidget) {
            QDateTime dateTime = dateTimeWidget->dateTime();
            model->setData(index, dateTime.toString(HistoryDialog::dateTimeFormat), Qt::EditRole);
        } else {
            qCWarning(KTT_LOG) << "Cast to QDateTimeEdit failed";
        }
    }

    void updateEditorGeometry(QWidget *editor, const QStyleOptionViewItem &option, const QModelIndex & /*index*/) const override
    {
        editor->setGeometry(option.rect);
    }
};

class ActivityTimelineWidget : public QWidget
{
public:
    struct Entry {
        QString task;
        QString description;
        QStringList comments;
        QDateTime start;
        QDateTime end;
        bool running;
    };

    explicit ActivityTimelineWidget(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        m_refreshTimer.setInterval(60000);
        connect(&m_refreshTimer, &QTimer::timeout, this, qOverload<>(&ActivityTimelineWidget::update));
    }

    QSize sizeHint() const override
    {
        return QSize(620, contentHeight());
    }

    void setEntries(const QList<Entry> &entries)
    {
        m_entries = entries;
        std::sort(m_entries.begin(), m_entries.end(), [](const Entry &left, const Entry &right) {
            return left.start > right.start;
        });
        const bool hasRunningEntry = std::any_of(entries.cbegin(), entries.cend(), [](const Entry &entry) {
            return entry.running;
        });
        if (hasRunningEntry) {
            m_refreshTimer.start();
        } else {
            m_refreshTimer.stop();
        }
        setMinimumHeight(contentHeight());
        updateGeometry();
        update();
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event)
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillRect(rect(), palette().base());

        const int axisX = 24;
        const int contentX = 50;
        const int right = width() - 16;
        if (right <= contentX) {
            return;
        }
        if (m_entries.isEmpty()) {
            painter.setPen(palette().text().color());
            painter.drawText(rect(), Qt::AlignCenter, tr("No activity recorded"));
            return;
        }

        QHash<QDate, double> dailyTotals;
        for (const Entry &entry : std::as_const(m_entries)) {
            dailyTotals[entry.start.date()] += entry.start.secsTo(effectiveEnd(entry)) / 60.0;
        }

        const QPen spinePen(palette().midlight().color(), 2);
        painter.setPen(spinePen);
        painter.drawLine(axisX, 18, axisX, height() - 18);

        int y = 14;
        QDate previousDate;
        QDateTime newerBoundary;
        for (const Entry &entry : std::as_const(m_entries)) {
            const QDateTime end = effectiveEnd(entry);
            if (newerBoundary.isValid() && end < newerBoundary) {
                const qint64 gapSeconds = end.secsTo(newerBoundary);
                if (gapSeconds >= 12 * 60 * 60) {
                    const QString gap = i18n("%1 without tracked activity", formatTime(gapSeconds / 60.0));
                    painter.setPen(palette().text().color());
                    painter.drawText(QRect(contentX, y, right - contentX, 24), Qt::AlignLeft | Qt::AlignVCenter, gap);
                    y += 28;
                }
            }

            if (entry.start.date() != previousDate) {
                QFont dateFont = painter.font();
                dateFont.setWeight(QFont::DemiBold);
                painter.setFont(dateFont);
                painter.setPen(palette().text().color());
                const int totalWidth = 140;
                const QRect dateRect(contentX, y, right - contentX - totalWidth, 24);
                const QRect totalRect(right - totalWidth, y, totalWidth, 24);
                painter.drawText(dateRect, Qt::AlignLeft | Qt::AlignVCenter, QLocale().toString(entry.start.date(), QLocale::LongFormat));
                painter.drawText(totalRect, Qt::AlignRight | Qt::AlignVCenter, i18n("Total %1", formatTime(dailyTotals.value(entry.start.date()))));
                painter.setFont(font());
                y += 28;
                previousDate = entry.start.date();
            }

            const int cardHeight = entryCardHeight(entry);
            const QRect card(contentX, y, right - contentX, cardHeight);
            painter.setPen(palette().midlight().color());
            painter.setBrush(palette().alternateBase());
            painter.drawRoundedRect(card, 4, 4);

            const int markerY = y + cardHeight / 2;
            painter.setPen(palette().base().color());
            painter.setBrush(palette().highlight());
            painter.drawEllipse(QPoint(axisX, markerY), 5, 5);

            const int textX = card.left() + 12;
            const int textWidth = card.width() - 24;
            QFont taskFont = painter.font();
            taskFont.setWeight(QFont::DemiBold);
            painter.setFont(taskFont);
            painter.setPen(palette().text().color());
            const QFontMetrics taskMetrics(taskFont);
            painter.drawText(QRect(textX, y + 5, textWidth, 22), Qt::AlignLeft | Qt::AlignVCenter,
                             taskMetrics.elidedText(entry.task, Qt::ElideRight, textWidth));

            painter.setFont(font());
            painter.setPen(palette().text().color());
            const QString startTime = entry.start.toString(QStringLiteral("HH:mm"));
            const QString endTime = entry.running ? tr("Now")
                                                  : (entry.start.date() == end.date() ? end.toString(QStringLiteral("HH:mm"))
                                                                                     : end.toString(QStringLiteral("MMM d HH:mm")));
            const QString duration = formatTime(entry.start.secsTo(end) / 60.0);
            const QString details = QStringLiteral("%1 - %2  (%3)").arg(startTime, endTime, duration);
            painter.drawText(QRect(textX, y + 27, textWidth, 20), Qt::AlignLeft | Qt::AlignVCenter, details);

            int extraTextY = y + 49;
            const QFontMetrics extraTextMetrics(painter.font());
            if (!entry.description.isEmpty()) {
                const QRect descriptionRect(textX, extraTextY, textWidth, 16);
                painter.drawText(descriptionRect, Qt::AlignLeft | Qt::AlignVCenter,
                                 extraTextMetrics.elidedText(entry.description, Qt::ElideRight, textWidth));
                extraTextY += 16;
            }
            for (const QString &comment : entry.comments) {
                const QRect commentRect(textX, extraTextY, textWidth, 16);
                painter.drawText(commentRect, Qt::AlignLeft | Qt::AlignVCenter,
                                 extraTextMetrics.elidedText(comment, Qt::ElideRight, textWidth));
                extraTextY += 16;
            }
            y += cardHeight + 8;

            if (!newerBoundary.isValid() || entry.start < newerBoundary) {
                newerBoundary = entry.start;
            }
        }
    }

private:
    int entryCardHeight(const Entry &entry) const
    {
        return 62 + (entry.description.isEmpty() ? 0 : 16) + entry.comments.size() * 16;
    }

    QDateTime effectiveEnd(const Entry &entry) const
    {
        if (entry.running) {
            return QDateTime::currentDateTime();
        }
        return entry.end.isValid() ? entry.end : entry.start;
    }

    int contentHeight() const
    {
        if (m_entries.isEmpty()) {
            return 220;
        }

        int total = 28;
        QDate previousDate;
        QDateTime newerBoundary;
        for (const Entry &entry : std::as_const(m_entries)) {
            const QDateTime end = effectiveEnd(entry);
            if (newerBoundary.isValid() && end < newerBoundary && end.secsTo(newerBoundary) >= 12 * 60 * 60) {
                total += 28;
            }
            if (entry.start.date() != previousDate) {
                total += 28;
                previousDate = entry.start.date();
            }
            total += entryCardHeight(entry) + 8;
            if (!newerBoundary.isValid() || entry.start < newerBoundary) {
                newerBoundary = entry.start;
            }
        }
        return total;
    }

    QList<Entry> m_entries;
    QTimer m_refreshTimer;
};

HistoryDialog::HistoryDialog(QWidget *parent, ProjectModel *projectModel, Task *scopeTask)
    : QDialog(parent)
    , m_projectModel(projectModel)
    , m_scopeTask(scopeTask)
    , m_timelineWidget(nullptr)
    , m_ui()
{
    m_ui.setupUi(this);
    auto *timelineLayout = new QVBoxLayout(m_ui.timelineContainer);
    m_timelineWidget = new ActivityTimelineWidget(m_ui.timelineContainer);
    timelineLayout->setContentsMargins(0, 0, 0, 0);
    timelineLayout->addWidget(m_timelineWidget);
    m_ui.timelineScrollArea->setWidget(m_ui.timelineContainer);
    setWindowTitle(m_scopeTask ? i18nc("@title:window", "Task Activity: %1", m_scopeTask->name()) : i18n("Edit History"));
    setMinimumSize(900, 540);
    m_ui.activitySummary->setVisible(m_scopeTask);
    updateScopeSummary();

    setStyleSheet(QStringLiteral(R"(
        QFrame#activitySummary {
            background: palette(alternate-base);
            border: 1px solid palette(midlight);
            border-radius: 4px;
        }
        QLabel#activityMetricCaption {
            color: palette(mid);
            font-size: 9pt;
        }
        QLabel#activityMetricValue {
            color: palette(text);
            font-size: 16pt;
            font-weight: 600;
        }
        QTableWidget#historytablewidget {
            border: 1px solid palette(midlight);
            border-radius: 4px;
            alternate-background-color: palette(alternate-base);
        }
        QTableWidget#historytablewidget::item {
            padding: 6px 8px;
        }
        QLabel#activityEmptyState {
            color: palette(mid);
            font-size: 11pt;
        }
        QHeaderView::section {
            padding: 9px 10px;
            border: 0;
            border-bottom: 1px solid palette(midlight);
            background: palette(alternate-base);
            font-weight: 600;
        }
    )"));

    auto *historyWidgetDelegate = new HistoryWidgetDelegate(m_ui.historytablewidget);
    m_ui.historytablewidget->setItemDelegateForColumn(1, historyWidgetDelegate);
    m_ui.historytablewidget->setItemDelegateForColumn(2, historyWidgetDelegate);

    m_ui.historytablewidget->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed | QAbstractItemView::SelectedClicked);
    m_ui.historytablewidget->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_ui.historytablewidget->setSelectionMode(QAbstractItemView::SingleSelection);
    m_ui.historytablewidget->verticalHeader()->hide();
    m_ui.historytablewidget->verticalHeader()->setDefaultSectionSize(38);
    m_ui.historytablewidget->setAlternatingRowColors(true);
    m_ui.historytablewidget->setShowGrid(false);
    m_ui.historytablewidget->setWordWrap(false);
    m_ui.historytablewidget->setTextElideMode(Qt::ElideRight);
    m_ui.historytablewidget->setColumnCount(6);
    m_ui.historytablewidget->setHorizontalHeaderLabels(
        QStringList{i18n("Task"), i18n("Start"), i18n("End"), i18n("Duration"), i18n("Comment"), QStringLiteral("event UID")});
    m_ui.historytablewidget->setColumnHidden(5, true);
    m_ui.historytablewidget->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    m_ui.historytablewidget->setColumnWidth(0, 220);
    m_ui.historytablewidget->setColumnWidth(1, 175);
    m_ui.historytablewidget->setColumnWidth(2, 175);
    m_ui.historytablewidget->setColumnWidth(3, 100);
    m_ui.historytablewidget->setSortingEnabled(false);
    connect(m_ui.historytablewidget, &QTableWidget::cellChanged, this, &HistoryDialog::onCellChanged);
    listAllEvents();
    updateTimelineView();
    auto *deleteButton = m_ui.buttonbox->addButton(QString(), QDialogButtonBox::ActionRole);
    KStandardGuiItem::assign(deleteButton, KStandardGuiItem::Delete);
    connect(deleteButton, &QPushButton::clicked, this, &HistoryDialog::onDeleteClicked);
}

QString HistoryDialog::listAllEvents()
{
    QString err;
    m_visibleEvents.clear();
    QSet<QString> scopedTaskUids;
    if (m_scopeTask) {
        for (Task *candidate : m_projectModel->tasksModel()->getAllTasks()) {
            for (Task *ancestor = candidate; ancestor; ancestor = ancestor->parentTask()) {
                if (ancestor->uid() == m_scopeTask->uid()) {
                    scopedTaskUids.insert(candidate->uid());
                    break;
                }
            }
        }
    }

    for (Event *event : m_projectModel->eventsModel()->events()) {
        Task *eventTask = m_projectModel->tasksModel()->taskByUID(event->relatedTo());
        if (!eventTask) {
            qCWarning(KTT_LOG) << "Unable to load task for event" << event->uid();
            err = QStringLiteral("NoRelatedToForEvent");
            continue;
        }

        if (m_scopeTask && !scopedTaskUids.contains(eventTask->uid())) {
            continue;
        }
        m_visibleEvents.append(event);
    }
    std::sort(m_visibleEvents.begin(), m_visibleEvents.end(), [](const Event *left, const Event *right) {
        return left->dtStart() > right->dtStart();
    });

    for (const auto *event : std::as_const(m_visibleEvents)) {
        int row = m_ui.historytablewidget->rowCount();
        m_ui.historytablewidget->insertRow(row);
        Task *eventTask = m_projectModel->tasksModel()->taskByUID(event->relatedTo());
        const int indent = m_scopeTask ? qMax(0, eventTask->depth() - m_scopeTask->depth()) : eventTask->depth();
        auto *taskItem = new QTableWidgetItem(QString(indent * 2, QLatin1Char(' ')) + eventTask->name());
        taskItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        taskItem->setToolTip(eventTask->name());
        m_ui.historytablewidget->setItem(row, 0, taskItem);

        QDateTime start = event->dtStart();
        QDateTime end = event->dtEnd();
        auto *startItem = new QTableWidgetItem(start.toString(dateTimeFormat));
        startItem->setToolTip(i18n("Edit the date and time"));
        m_ui.historytablewidget->setItem(row, 1, startItem);
        auto *endItem = new QTableWidgetItem(event->hasEndDate() ? end.toString(dateTimeFormat) : i18n("Running"));
        endItem->setToolTip(i18n("Edit the date and time"));
        if (!event->hasEndDate()) {
            endItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        }
        m_ui.historytablewidget->setItem(row, 2, endItem);

        auto *durationItem = new QTableWidgetItem(event->hasEndDate() ? formatTime(event->duration() / 60) : i18n("Running"));
        durationItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        m_ui.historytablewidget->setItem(row, 3, durationItem);

        const QStringList comments = event->comments();
        auto *commentItem = new QTableWidgetItem(comments.isEmpty() ? QString() : comments.last());
        commentItem->setToolTip(comments.join(QLatin1Char('\n')));
        m_ui.historytablewidget->setItem(row, 4, commentItem);
        m_ui.historytablewidget->setItem(row, 5, new QTableWidgetItem(event->uid()));
    }

    const bool hasActivity = m_ui.historytablewidget->rowCount() > 0;
    m_ui.historytablewidget->setVisible(hasActivity);
    m_ui.activityEmptyState->setVisible(!hasActivity);
    return err;
}

void HistoryDialog::updateTimelineView()
{
    QList<ActivityTimelineWidget::Entry> entries;
    for (const Event *event : std::as_const(m_visibleEvents)) {
        Task *eventTask = m_projectModel->tasksModel()->taskByUID(event->relatedTo());
        if (!eventTask) {
            continue;
        }
        entries.append({eventTask->name(), eventTask->description(), event->comments(), event->dtStart(), event->dtEnd(), !event->hasEndDate()});
    }
    m_timelineWidget->setEntries(entries);
}

void HistoryDialog::updateScopeSummary()
{
    if (!m_scopeTask) {
        return;
    }

    m_ui.taskTimeValue->setText(formatTime(m_scopeTask->time()));
    m_ui.totalTimeValue->setText(formatTime(m_scopeTask->totalTime()));
}

void HistoryDialog::changeEvent(QEvent *e)
{
    QDialog::changeEvent(e);
    switch (e->type()) {
    case QEvent::LanguageChange:
        m_ui.retranslateUi(this);
        break;
    default:
        break;
    }
}

void HistoryDialog::onCellChanged(int row, int col)
{
    qCDebug(KTT_LOG) << "row =" << row << " col =" << col;

    if (!m_ui.historytablewidget->item(row, 5)) {
        // the program did the change, not the user
        return;
    }

    switch (col) {
    case 1: {
        // StartDate changed
        qCDebug(KTT_LOG) << "user changed StartDate to" << m_ui.historytablewidget->item(row, col)->text();
        QDateTime datetime = QDateTime::fromString(m_ui.historytablewidget->item(row, col)->text(), dateTimeFormat);
        if (!datetime.isValid()) {
            KMessageBox::information(nullptr, i18n("This is not a valid Date/Time."));
            break;
        }

        QString uid = m_ui.historytablewidget->item(row, 5)->text();
        Event *event = m_projectModel->eventsModel()->eventByUID(uid);
        if (!event) {
            break;
        }

        event->setDtStart(datetime);
        // setDtStart could modify date/time, sync it into our table
        m_ui.historytablewidget->item(row, col)->setText(event->dtStart().toString(dateTimeFormat));
        m_ui.historytablewidget->item(row, 3)->setText(event->hasEndDate() ? formatTime(event->duration() / 60) : i18n("Running"));

        Q_EMIT m_projectModel->eventsModel()->timesChanged();
        updateScopeSummary();
        updateTimelineView();
        qCDebug(KTT_LOG) << "Program SetDtStart to" << m_ui.historytablewidget->item(row, col)->text();
        break;
    }
    case 2: {
        // EndDate changed
        qCDebug(KTT_LOG) << "user changed EndDate to" << m_ui.historytablewidget->item(row, col)->text();
        QDateTime datetime = QDateTime::fromString(m_ui.historytablewidget->item(row, col)->text(), dateTimeFormat);
        if (!datetime.isValid()) {
            KMessageBox::information(nullptr, i18n("This is not a valid Date/Time."));
            break;
        }

        QString uid = m_ui.historytablewidget->item(row, 5)->text();
        Event *event = m_projectModel->eventsModel()->eventByUID(uid);
        if (!event) {
            break;
        }

        event->setDtEnd(datetime);
        // setDtStart could modify date/time, sync it into our table
        m_ui.historytablewidget->item(row, col)->setText(event->dtEnd().toString(dateTimeFormat));
        m_ui.historytablewidget->item(row, 3)->setText(formatTime(event->duration() / 60));

        Q_EMIT m_projectModel->eventsModel()->timesChanged();
        updateScopeSummary();
        updateTimelineView();
        qCDebug(KTT_LOG) << "Program SetDtEnd to" << m_ui.historytablewidget->item(row, col)->text();
        break;
    }
    case 4: {
        // Comment changed
        qCDebug(KTT_LOG) << "user changed Comment to" << m_ui.historytablewidget->item(row, col)->text();

        QString comment = m_ui.historytablewidget->item(row, col)->text();
        if (comment.trimmed().isEmpty()) {
            break;
        }

        QString uid = m_ui.historytablewidget->item(row, 5)->text();
        Event *event = m_projectModel->eventsModel()->eventByUID(uid);
        if (event) {
            event->addComment(comment);
            m_ui.historytablewidget->item(row, col)->setToolTip(event->comments().join(QLatin1Char('\n')));
            updateTimelineView();
        }
        break;
    }
    default:
        break;
    }
}

void HistoryDialog::refresh()
{
    while (m_ui.historytablewidget->rowCount() > 0) {
        m_ui.historytablewidget->removeRow(0);
    }
    listAllEvents();
    updateTimelineView();
}

void HistoryDialog::onDeleteClicked()
{
    // Create a list of rows to delete.
    // This includes both the current row (i.e. the row the cursor is on) as
    // well as the rows that are part of the current selection. Because these
    // can overlap and because the selected items contain one item per cell, not
    // just per row, we use a QSet here to get a unique list of rows.
    QSet<int> rows;
    if (m_ui.historytablewidget->currentRow() >= 0) { // Could be -1 if table is empty or no row is selected
        rows.insert(m_ui.historytablewidget->currentRow());
    }
    for (const auto &item : m_ui.historytablewidget->selectedItems()) {
        rows.insert(item->row());
    }

    if (rows.empty()) {
        KMessageBox::information(this, i18n("Please select a task to delete."));
        return;
    }

    bool refresh = false;
    for (int row : rows) {
        QString uid = m_ui.historytablewidget->item(row, 5)->text();
        qDebug() << "uid =" << uid;
        const Event *event = m_projectModel->eventsModel()->eventByUID(uid);
        if (event) {
            qCDebug(KTT_LOG) << "removing uid " << event->uid();
            m_projectModel->eventsModel()->removeByUID(event->uid());
            refresh = true;
        }
    }
    if (refresh) {
        Q_EMIT m_projectModel->eventsModel()->timesChanged();
        this->refresh();
    }
}

void HistoryDialog::on_buttonbox_rejected()
{
    m_ui.historytablewidget->setCurrentCell(0, 0); // you need to change the cell to store the value
    close();
}

#include "moc_historydialog.cpp"
