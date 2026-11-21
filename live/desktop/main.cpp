#include "BatchQueue.hpp"

#include <QAbstractItemView>
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QMimeData>
#include <QPainter>
#include <QPushButton>
#include <QSpinBox>
#include <QStatusBar>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QStringList>
#include <QTableWidget>
#include <QTimer>
#include <QUrl>
#include <QVariant>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QWidget>

#include <algorithm>

namespace {
using namespace bq;
using Clock = Job::Clock;

enum Column { File, Status, Size, Priority, Dispatch, Crc, Pid, Wait, Run, Timeline, Columns };
enum Role { JobId = Qt::UserRole, Start, Queued, Processing };

double milliseconds(Clock::duration duration) {
    return std::chrono::duration<double, std::milli>(duration).count();
}

QString duration(double ms) {
    if (ms < 1000)
        return QString::number(ms, 'f', 1) + " ms";
    return QString::number(ms / 1000, 'f', 2) + " s";
}

QString stateName(State state) {
    switch (state) {
        case State::Queued: return "Queued";
        case State::Running: return "Running";
        case State::Completed: return "Completed";
        case State::Failed: return "Failed";
        case State::Cancelled: return "Cancelled";
    }
    return {};
}

QString policyName(Policy policy) {
    switch (policy) {
        case Policy::Fcfs: return "FCFS";
        case Policy::Sjf: return "SJF";
        case Policy::Priority: return "Priority";
    }
    return {};
}

struct Timing {
    double offset;
    double waiting;
    double running;
    bool started;
};

Timing timing(const Job& job, Clock::time_point origin, Clock::time_point now) {
    const bool started = job.started != Clock::time_point{};
    const auto end = job.finished == Clock::time_point{} ? now : job.finished;
    return {milliseconds(job.submitted - origin),
            milliseconds((started ? job.started : end) - job.submitted),
            started ? milliseconds(end - job.started) : 0, started};
}

QStringList fileLocations() {
    QStringList paths{QDir::rootPath(), QDir::homePath()};
    const QDir mounts("/mnt");
    if (!mounts.exists())
        return paths;
    paths.append(mounts.absolutePath());
    for (const auto& name : mounts.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (name.size() != 1 || !name.front().isLetter())
            continue;
        const auto drive = mounts.absoluteFilePath(name);
        paths.append(drive);
        const QDir profiles(drive + "/Users");
        if (!profiles.exists())
            continue;
        paths.append(profiles.absolutePath());
        const auto profileName = QDir(QDir::homePath()).dirName();
        const auto current = QDir::currentPath();
        for (const auto& profile : profiles.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            const auto folder = profiles.absoluteFilePath(profile);
            if (profile.compare(profileName, Qt::CaseInsensitive) != 0 &&
                current != folder && !current.startsWith(folder + '/'))
                continue;
            paths.append(folder);
            for (const auto* name : {"Desktop", "Documents", "Downloads", "OneDrive"}) {
                const auto location = folder + '/' + name;
                if (QDir(location).exists())
                    paths.append(location);
            }
        }
    }
    paths.removeDuplicates();
    return paths;
}

class TimelineDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override {
        QStyleOptionViewItem background(option);
        initStyleOption(&background, index);
        background.text.clear();
        auto style = background.widget ? background.widget->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &background, painter, background.widget);
        QRectF track = QRectF(option.rect).adjusted(6, 6, -6, -6);
        if (track.isEmpty())
            return;
        painter->save();
        painter->setClipRect(track);
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor("#e5edf2"));
        painter->drawRoundedRect(track, 4, 4);
        auto left = track.left() + track.width() * index.data(Start).toDouble();
        auto waiting = track.width() * index.data(Queued).toDouble();
        auto running = track.width() * index.data(Processing).toDouble();
        painter->setBrush(QColor("#c78332"));
        painter->drawRoundedRect(QRectF(left, track.top(), waiting, track.height()), 3, 3);
        painter->setBrush(QColor("#18796b"));
        painter->drawRoundedRect(QRectF(left + waiting, track.top(), running, track.height()), 3, 3);
        painter->restore();
    }
};

class Desktop : public QWidget {
    BatchQueue queue_{4, 2, [](const std::string& file) { return Checksum::runPath(file); },
                      Input::AbsolutePaths, [](const std::string& file) { return Checksum::pathSize(file); }};
    QTableWidget* table_ = new QTableWidget(this);
    QLabel* summary_ = new QLabel(this);
    QLabel* solver_ = new QLabel(this);
    QLabel* timelineScale_ = new QLabel(this);
    QLabel* policyHelp_ = new QLabel(this);
    QLabel* priorityLabel_ = new QLabel("Batch priority", this);
    QSpinBox* priority_ = new QSpinBox(this);
    QPushButton* replay_ = new QPushButton("Rerun batch", this);
    QCheckBox* showEarlier_ = new QCheckBox("Show previous batch", this);
    QStatusBar* status_ = new QStatusBar(this);
    QFileDialog* picker_ = nullptr;
    std::uint64_t firstCurrentJob_ = 0;
    std::uint64_t firstPreviousJob_ = 0;

public:
    Desktop() {
        setWindowTitle("BuildQueue | Desktop checksum queue");
        setObjectName("dashboard");
        resize(1160, 690);
        setAcceptDrops(true);
        setStyleSheet(R"(
            QWidget#dashboard { background: #f0f4f7; color: #1a3445; }
            QWidget#hero { background: #123545; border-radius: 14px; }
            QWidget#hero QLabel { color: #d1e4ea; font-size: 14px; }
            QLabel#heroTitle { color: #ffffff; font-size: 29px; font-weight: 700; }
            QPushButton {
                padding: 7px 13px; color: #193c4e; background: #ffffff;
                border: 1px solid #aebfc9; border-radius: 6px;
            }
            QPushButton:hover { background: #e6f4f0; }
            QPushButton:disabled { color: #536c77; background: #e9eff2; }
            QPushButton:focus { border-color: #18796b; }
            QPushButton[policyChoice="true"]:checked, QPushButton#addFiles {
                color: white; background: #106a5f; border-color: #106a5f; font-weight: 600;
            }
            QPushButton#addFiles:hover { background: #177d70; }
            QSpinBox { padding: 5px; }
            QTableWidget {
                color: #203c4c; background: #ffffff; alternate-background-color: #f6f9fb;
                border: 1px solid #c8d6de; selection-background-color: #d5ebe7;
                selection-color: #133744;
            }
            QTableWidget::item { padding: 0 7px; }
            QHeaderView::section {
                background: #e3edf1; color: #254759; border: 0;
                border-bottom: 1px solid #c8d6de; padding: 8px 7px; font-weight: 600;
            }
            QLabel#summary, QLabel#solver {
                background: #ffffff; border: 1px solid #d0dde3; border-radius: 8px;
                padding: 11px; color: #244759;
            }
            QLabel#summary { font-weight: 600; }
            QLabel#hint { color: #436171; }
        )");
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(22, 22, 22, 12);
        layout->setSpacing(12);
        auto* hero = new QWidget(this);
        hero->setObjectName("hero");
        auto* heroLayout = new QHBoxLayout(hero);
        heroLayout->setContentsMargins(22, 18, 22, 18);
        auto* intro = new QVBoxLayout;
        auto* heading = new QLabel("BuildQueue", hero);
        heading->setObjectName("heroTitle");
        intro->addWidget(heading);
        intro->addWidget(new QLabel("File checksum queue", hero));
        heroLayout->addLayout(intro);
        heroLayout->addStretch();
        auto* add = new QPushButton("Add files", hero);
        add->setObjectName("addFiles");
        add->setToolTip("Browse local and mounted Windows drives, or drop files on the window.");
        heroLayout->addWidget(add);
        layout->addWidget(hero);

        auto* controls = new QHBoxLayout;
        auto* policy = new QButtonGroup(this);
        auto addPolicy = [&](const QString& name, Policy value) {
            auto* button = new QPushButton(name, this);
            button->setProperty("policyChoice", true);
            button->setCheckable(true);
            policy->addButton(button, static_cast<int>(value));
            controls->addWidget(button);
            return button;
        };
        controls->addWidget(new QLabel("Scheduling", this));
        auto* fcfs = addPolicy("FCFS", Policy::Fcfs);
        fcfs->setAccessibleName("First come, first served");
        fcfs->setChecked(true);
        addPolicy("SJF", Policy::Sjf)->setAccessibleName("Shortest job first");
        addPolicy("Priority", Policy::Priority);
        priority_->setRange(0, 9);
        priority_->setToolTip("0 is the highest priority");
        auto* cancel = new QPushButton("Cancel selected", this);
        auto* clear = new QPushButton("Clear finished", this);
        controls->addSpacing(10);
        controls->addWidget(priorityLabel_);
        controls->addWidget(priority_);
        controls->addStretch();
        controls->addWidget(cancel);
        controls->addWidget(clear);
        layout->addLayout(controls);
        policyHelp_->setObjectName("hint");
        policyHelp_->setWordWrap(true);
        layout->addWidget(policyHelp_);
        summary_->setObjectName("summary");
        layout->addWidget(summary_);
        auto* historyControls = new QHBoxLayout;
        replay_->setToolTip("Rerun the current batch under the selected algorithm after all jobs finish.");
        historyControls->addWidget(replay_);
        showEarlier_->setToolTip("Compare the previous batch on its own elapsed-time scale.");
        historyControls->addWidget(showEarlier_);
        historyControls->addStretch();
        layout->addLayout(historyControls);

        table_->setColumnCount(Columns);
        table_->setHorizontalHeaderLabels({"File", "Status", "Size (B)", "Priority", "Dispatch", "CRC checksum",
                                           "PID", "Wait", "Run", "Timeline"});
        table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table_->setSelectionMode(QAbstractItemView::SingleSelection);
        table_->setSelectionBehavior(QAbstractItemView::SelectRows);
        table_->setAlternatingRowColors(true);
        table_->setShowGrid(false);
        table_->verticalHeader()->setDefaultSectionSize(35);
        table_->verticalHeader()->hide();
        table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
        table_->horizontalHeader()->setSectionResizeMode(File, QHeaderView::Stretch);
        table_->horizontalHeader()->setSectionResizeMode(Timeline, QHeaderView::Stretch);
        table_->setItemDelegateForColumn(Timeline, new TimelineDelegate(table_));
        layout->addWidget(table_, 1);
        timelineScale_->setObjectName("hint");
        layout->addWidget(timelineScale_);

        solver_->setObjectName("solver");
        solver_->setTextFormat(Qt::PlainText);
        solver_->setWordWrap(true);
        layout->addWidget(solver_);
        layout->addWidget(status_);

        connect(add, &QPushButton::clicked, this, [this] { chooseFiles(); });
        connect(policy, &QButtonGroup::idClicked, this, [this](int id) {
            choosePolicy(static_cast<Policy>(id));
        });
        connect(cancel, &QPushButton::clicked, this, [this] {
            const auto* item = table_->item(table_->currentRow(), File);
            if (!item || !queue_.cancel(item->data(JobId).toULongLong()))
                status_->showMessage("Select a queued job to cancel.", 5000);
            refresh();
        });
        connect(clear, &QPushButton::clicked, this, [this] {
            queue_.clearFinished();
            refresh();
        });
        connect(replay_, &QPushButton::clicked, this, [this] { rerunBatch(); });
        connect(showEarlier_, &QCheckBox::toggled, this, [this] { refresh(); });
        auto* timer = new QTimer(this);
        connect(timer, &QTimer::timeout, this, [this] { refresh(); });
        timer->start(300);
        choosePolicy(Policy::Fcfs);
        refresh();
    }

protected:
    void dragEnterEvent(QDragEnterEvent* event) override {
        const auto urls = event->mimeData()->urls();
        if (std::any_of(urls.cbegin(), urls.cend(), [](const QUrl& url) { return url.isLocalFile(); }))
            event->acceptProposedAction();
    }

    void dropEvent(QDropEvent* event) override {
        QStringList paths;
        for (const auto& url : event->mimeData()->urls())
            if (url.isLocalFile())
                paths.append(url.toLocalFile());
        if (!paths.isEmpty()) {
            enqueue(paths);
            event->acceptProposedAction();
        }
    }

private:
    void chooseFiles() {
        if (!picker_) {
            picker_ = new QFileDialog(this);
            picker_->setOption(QFileDialog::DontUseNativeDialog);
            picker_->setWindowModality(Qt::NonModal);
            picker_->setWindowTitle("Add files to BuildQueue");
            picker_->setFileMode(QFileDialog::ExistingFiles);
            picker_->setViewMode(QFileDialog::Detail);
            picker_->resize(820, 480);
            QList<QUrl> shortcuts;
            for (const auto& path : fileLocations())
                shortcuts.append(QUrl::fromLocalFile(path));
            picker_->setSidebarUrls(shortcuts);
            if (QDir("/mnt/c").exists())
                picker_->setDirectory("/mnt/c");
            connect(picker_, &QFileDialog::accepted, this, [this] {
                enqueue(picker_->selectedFiles());
            });
        }
        picker_->show();
        picker_->raise();
        picker_->activateWindow();
    }

    void choosePolicy(Policy policy) {
        queue_.setPolicy(policy);
        priority_->setEnabled(policy == Policy::Priority);
        priorityLabel_->setEnabled(policy == Policy::Priority);
        switch (policy) {
            case Policy::Fcfs: policyHelp_->setText("First come, first served. Jobs start in submission order."); break;
            case Policy::Sjf: policyHelp_->setText("Shortest job first. The smallest waiting file starts when a slot opens."); break;
            case Policy::Priority: policyHelp_->setText("Priority scheduling. Set the priority before adding each batch. 0 is highest."); break;
        }
    }

    void rerunBatch() {
        const auto snapshot = queue_.snapshot();
        if (std::any_of(snapshot.jobs.begin(), snapshot.jobs.end(), [](const Job& job) {
                return job.state == State::Queued || job.state == State::Running;
            }))
            return;
        std::vector<JobRequest> files;
        for (const auto& job : snapshot.jobs)
            if (job.id >= firstCurrentJob_ && job.state != State::Cancelled)
                files.push_back({job.file, job.priority});
        if (files.empty())
            return;

        try {
            const auto ids = queue_.submit(files);
            firstPreviousJob_ = firstCurrentJob_;
            firstCurrentJob_ = ids.front();
            showEarlier_->setChecked(false);
            status_->showMessage("Rerunning the batch with " + policyName(snapshot.policy) + ".", 5000);
            refresh();
        } catch (const std::exception& error) {
            status_->showMessage(QString::fromUtf8(error.what()), 7000);
        }
    }

    void enqueue(const QStringList& paths) {
        if (paths.isEmpty())
            return;
        std::vector<std::string> files;
        files.reserve(paths.size());
        for (const auto& path : paths) {
            const QFileInfo info(path);
            if (!info.isFile() || info.isSymLink()) {
                status_->showMessage("Choose regular files. Unavailable files and symlinks are skipped.", 7000);
                continue;
            }
            files.emplace_back(QFile::encodeName(info.absoluteFilePath()).toStdString());
        }
        if (files.empty())
            return;
        try {
            queue_.submit(files, priority_->value());
            showEarlier_->setChecked(false);
            status_->showMessage(QString("Queued %1 file(s). Skipped %2.")
                .arg(static_cast<qlonglong>(files.size()))
                .arg(static_cast<qlonglong>(paths.size()) - static_cast<qlonglong>(files.size())), 5500);
            refresh();
        } catch (const std::exception& error) {
            status_->showMessage(QString::fromUtf8(error.what()), 7000);
        }
    }

    QTableWidgetItem* cell(int row, int column, const QString& value) {
        auto* item = table_->item(row, column);
        if (!item) {
            item = new QTableWidgetItem;
            table_->setItem(row, column, item);
        }
        if (item->text() != value)
            item->setText(value);
        return item;
    }

    void refresh() {
        auto snapshot = queue_.snapshot();
        const auto current = std::find_if(snapshot.jobs.begin(), snapshot.jobs.end(), [&](const Job& job) {
            return job.id >= firstCurrentJob_;
        });
        const auto previous = std::find_if(snapshot.jobs.begin(), current, [&](const Job& job) {
            return job.id >= firstPreviousJob_;
        });
        const bool busy = std::any_of(snapshot.jobs.begin(), snapshot.jobs.end(), [](const Job& job) {
            return job.state == State::Queued || job.state == State::Running;
        });
        replay_->setEnabled(!busy && std::any_of(current, snapshot.jobs.end(),
                                                [](const Job& job) { return job.state != State::Cancelled; }));
        const bool hasPrevious = previous != current;
        const bool viewingPrevious = hasPrevious && showEarlier_->isChecked();
        showEarlier_->setEnabled(hasPrevious);
        if (viewingPrevious) {
            snapshot.jobs.erase(current, snapshot.jobs.end());
            snapshot.jobs.erase(snapshot.jobs.begin(), previous);
        } else {
            snapshot.jobs.erase(snapshot.jobs.begin(), current);
        }
        const auto now = Clock::now();
        const auto origin = snapshot.jobs.empty() ? now : snapshot.jobs.front().submitted;
        std::vector<Timing> timings;
        timings.reserve(snapshot.jobs.size());
        double span = 1;
        for (const auto& job : snapshot.jobs) {
            const auto times = timing(job, origin, now);
            timings.push_back(times);
            span = std::max(span, times.offset + times.waiting + times.running);
        }

        const auto* selected = table_->item(table_->currentRow(), File);
        const auto selectedId = selected ? selected->data(JobId).toULongLong() : 0;
        table_->setRowCount(static_cast<int>(snapshot.jobs.size()));
        int selectedRow = -1;
        int completed = 0;
        int queued = 0;
        double waitTotal = 0;
        double runTotal = 0;
        for (int row = 0; row < table_->rowCount(); ++row) {
            const auto& job = snapshot.jobs[row];
            const auto& times = timings[row];
            const QString path = QFile::decodeName(job.file.c_str());
            auto* file = cell(row, File, QFileInfo(path).fileName());
            file->setToolTip(path);
            file->setData(JobId, QVariant::fromValue<qulonglong>(job.id));
            if (job.id == selectedId)
                selectedRow = row;
            auto* state = cell(row, Status, stateName(job.state));
            state->setToolTip(QString::fromStdString(job.result.error));
            if (job.state == State::Completed) {
                ++completed;
                waitTotal += times.waiting;
                runTotal += times.running;
            }
            if (job.state == State::Queued)
                ++queued;
            state->setForeground(QColor(job.state == State::Failed ? "#a33242" :
                job.state == State::Completed ? "#116152" :
                job.state == State::Queued ? "#80500f" : "#244759"));
            cell(row, Size, job.estimatedBytes
                ? QString::number(static_cast<qulonglong>(*job.estimatedBytes)) : QString());
            cell(row, Priority, QString::number(job.priority));
            cell(row, Dispatch, times.started
                ? QString("#%1 � %2").arg(static_cast<qulonglong>(job.dispatchOrder)).arg(policyName(job.dispatchPolicy))
                : QString());
            cell(row, Crc, job.state == State::Completed ? QString::number(job.result.checksum) : QString());
            cell(row, Pid, job.result.pid > 0 ? QString::number(job.result.pid) : QString());
            cell(row, Wait, duration(times.waiting));
            cell(row, Run, times.started ? duration(times.running) : QString());
            auto* bar = cell(row, Timeline, {});
            bar->setData(Start, times.offset / span);
            bar->setData(Queued, times.waiting / span);
            bar->setData(Processing, times.running / span);
            bar->setToolTip(QString("Submitted at %1, queued for %2, processing %3")
                .arg(duration(times.offset), duration(times.waiting),
                     times.started ? duration(times.running) : QString("pending")));
        }
        if (selectedId && selectedRow != table_->currentRow())
            table_->setCurrentCell(selectedRow, selectedRow < 0 ? -1 : File);
        auto summary = QString("%1 queued     %2 completed     %3/%4 slots active     Peak %5     %6 workers")
            .arg(queued).arg(completed).arg(snapshot.running).arg(snapshot.slots)
            .arg(snapshot.peak).arg(snapshot.workers);
        if (completed > 0)
            summary += QString("     Avg wait %1     Avg run %2")
                .arg(duration(waitTotal / completed), duration(runTotal / completed));
        summary_->setText(summary);
        timelineScale_->setText(QString("%1 timeline: 0 to %2  |  Amber: queued  |  Green: processing")
            .arg(viewingPrevious ? "Previous batch" : "Current batch", duration(span)));
        auto solverText = QString("Deadlock solver: wait-for graph cycle detection  |  Recovery: reject the request that closes the cycle  |  Detected: %1")
            .arg(static_cast<qulonglong>(snapshot.deadlocks.count));
        if (!snapshot.deadlocks.lastCycle.empty())
            solverText += "\n" + QString::fromStdString(snapshot.deadlocks.lastCycle);
        solver_->setText(solverText);
    }
};
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    Desktop window;
    window.show();
    return app.exec();
}
