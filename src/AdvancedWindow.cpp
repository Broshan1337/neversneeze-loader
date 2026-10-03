#include "AdvancedWindow.h"

#include <QCloseEvent>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QSettings>
#include <QVBoxLayout>

AdvancedWindow::AdvancedWindow(Injector &injector, const QString &projectRoot, QWidget *parent)
    : QDialog(parent)
    , m_injector{injector}
    , m_root{projectRoot}
{
    setWindowTitle(QStringLiteral("Neversnooze - Advanced"));
    setModal(false);
    setMinimumWidth(560);

    buildUi();
    loadSettings();
    refreshTargets();

    connect(&m_autoTimer, &QTimer::timeout, this, &AdvancedWindow::autoTick);
    m_autoTimer.start(1000);

    connect(&m_injector, &Injector::anyFinished, this, &AdvancedWindow::onJobFinished);
    connect(&m_injector, &Injector::protonFinished, this, &AdvancedWindow::onJobFinished);
}

void AdvancedWindow::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setSpacing(10);

    // target row
    auto *targetRow = new QHBoxLayout;
    targetRow->setSpacing(8);
    m_targetBox = new QComboBox(this);
    m_targetBox->addItem(QStringLiteral("(press refresh to pick a target)"));
    auto *refreshBtn = new QPushButton(QStringLiteral("Refresh"), this);
    targetRow->addWidget(new QLabel(QStringLiteral("Target:"), this), 0);
    targetRow->addWidget(m_targetBox, 1);
    targetRow->addWidget(refreshBtn);
    root->addLayout(targetRow);

    // dll list: checked = injected, order = injection order; suffix picks the pipeline
    m_dllList = new QListWidget(this);
    m_dllList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_dllList->setToolTip(QStringLiteral("Checked entries are injected, in list order. .dll = Proton pipeline, .so = native pipeline."));
    root->addWidget(m_dllList, 1);

    // list management row
    auto *listRow = new QHBoxLayout;
    listRow->setSpacing(8);
    m_addBtn = new QPushButton(QStringLiteral("Add DLL / SO"), this);
    m_removeBtn = new QPushButton(QStringLiteral("Remove"), this);
    m_upBtn = new QPushButton(QStringLiteral("\u2191"), this);
    m_downBtn = new QPushButton(QStringLiteral("\u2193"), this);
    listRow->addWidget(m_addBtn);
    listRow->addWidget(m_removeBtn);
    listRow->addWidget(m_upBtn);
    listRow->addWidget(m_downBtn);
    root->addLayout(listRow);

    // auto row
    auto *autoRow = new QHBoxLayout;
    autoRow->setSpacing(8);
    m_autoBox = new QCheckBox(QStringLiteral("Automatically inject when the target starts"), this);
    m_delaySpin = new QSpinBox(this);
    m_delaySpin->setRange(0, 120);
    m_delaySpin->setSuffix(QStringLiteral("s"));
    m_delaySpin->setPrefix(QStringLiteral("Delay: "));
    autoRow->addWidget(m_autoBox, 1);
    autoRow->addWidget(m_delaySpin);
    root->addLayout(autoRow);

    // inject + status
    m_injectBtn = new QPushButton(QStringLiteral("INJECT NOW"), this);
    m_injectBtn->setObjectName(QStringLiteral("InjectBtn"));
    m_injectBtn->setCursor(Qt::PointingHandCursor);
    root->addWidget(m_injectBtn);
    m_statusLabel = new QLabel(QStringLiteral("Injected 0 DLL(s)."), this);
    m_statusLabel->setStyleSheet(QStringLiteral("color:#8a8f9a; font-size:11px;"));
    root->addWidget(m_statusLabel);

    connect(refreshBtn, &QPushButton::clicked, this, &AdvancedWindow::refreshTargets);
    connect(m_addBtn, &QPushButton::clicked, this, &AdvancedWindow::addDll);
    connect(m_removeBtn, &QPushButton::clicked, this, &AdvancedWindow::removeSelected);
    connect(m_upBtn, &QPushButton::clicked, this, [this] { moveSelected(-1); });
    connect(m_downBtn, &QPushButton::clicked, this, [this] { moveSelected(1); });
    connect(m_injectBtn, &QPushButton::clicked, this, &AdvancedWindow::injectNow);
    connect(m_autoBox, &QCheckBox::toggled, this, &AdvancedWindow::onAutoToggled);
}

void AdvancedWindow::refreshTargets()
{
    const QString previousKey = targetKey();

    m_targetBox->clear();
    const auto proton = m_injector.listProtonProcesses();
    for (const auto &game : proton) {
        const QString dist = game.wineBinary.section(QStringLiteral("/files/"), 0, 0);
        m_targetBox->addItem(QStringLiteral("Proton: %1  (%2)").arg(game.gameExe, QFileInfo(dist).fileName()),
                             QStringLiteral("proton:%1").arg(game.gameExe));
    }
    const auto processes = m_injector.listInjectableProcesses();
    for (const auto &p : processes)
        m_targetBox->addItem(QStringLiteral("Process: %1  (%2)").arg(p.name).arg(p.pid),
                             QStringLiteral("native:%1").arg(p.name));

    if (m_targetBox->count() == 0)
        m_targetBox->addItem(QStringLiteral("(no targets - start a game)"));

    // restore the previous selection when the same target is still alive
    if (!previousKey.isEmpty())
        for (int i = 0; i < m_targetBox->count(); ++i)
            if (m_targetBox->itemData(i).toString() == previousKey) {
                m_targetBox->setCurrentIndex(i);
                break;
            }
}

QString AdvancedWindow::targetKey() const
{
    return m_targetBox->currentData().toString();
}

void AdvancedWindow::addDll()
{
    const QString path = QFileDialog::getOpenFileName(this,
        QStringLiteral("Add a library"),
        m_root,
        QStringLiteral("Libraries (*.dll *.so);;All files (*)"));
    if (path.isEmpty())
        return;
    auto *item = new QListWidgetItem(QFileInfo(path).fileName(), m_dllList);
    item->setData(Qt::UserRole, path);
    item->setToolTip(path);
    item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
    item->setCheckState(Qt::Checked);
    saveSettings();
}

void AdvancedWindow::removeSelected()
{
    const auto selected = m_dllList->selectedItems();
    for (auto *item : selected)
        delete item;
    saveSettings();
}

void AdvancedWindow::moveSelected(int delta)
{
    const int row = m_dllList->currentRow();
    if (row < 0)
        return;
    const int target = row + delta;
    if (target < 0 || target >= m_dllList->count())
        return;
    auto *item = m_dllList->takeItem(row);
    m_dllList->insertItem(target, item);
    m_dllList->setCurrentRow(target);
    saveSettings();
}

void AdvancedWindow::injectNow()
{
    if (m_queueBusy)
        return;

    const QString key = targetKey();
    if (key.isEmpty()) {
        m_statusLabel->setText(QStringLiteral("No target - refresh and pick one."));
        return;
    }

    // collect the checked entries in order; the suffix picks the pipeline
    m_jobQueue.clear();
    for (int i = 0; i < m_dllList->count(); ++i) {
        const auto *item = m_dllList->item(i);
        if (item->checkState() != Qt::Checked)
            continue;
        const QString path = item->data(Qt::UserRole).toString();
        const bool proton = path.endsWith(QStringLiteral(".dll"), Qt::CaseInsensitive);
        m_jobQueue.append({proton ? QStringLiteral("proton") : QStringLiteral("native"), path});
    }
    if (m_jobQueue.isEmpty()) {
        m_statusLabel->setText(QStringLiteral("Nothing checked to inject."));
        return;
    }

    m_injectedCount = 0;
    m_queueBusy = true;
    m_injectBtn->setEnabled(false);
    m_lastError.clear();
    m_targetKey = key;
    startNextJob();
}

void AdvancedWindow::startNextJob()
{
    if (m_jobQueue.isEmpty()) {
        m_queueBusy = false;
        m_injectBtn->setEnabled(true);
        m_statusLabel->setText(m_lastError.isEmpty()
            ? QStringLiteral("Injected %1 DLL(s).").arg(m_injectedCount)
            : QStringLiteral("Injected %1 DLL(s). Last error: %2").arg(m_injectedCount).arg(m_lastError));
        return;
    }

    const auto [pipeline, path] = m_jobQueue.front();
    if (pipeline == QStringLiteral("proton")) {
        const QString wanted = m_targetKey.mid(QStringLiteral("proton:").size());
        for (const auto &game : m_injector.listProtonProcesses()) {
            if (game.gameExe.compare(wanted, Qt::CaseInsensitive) == 0) {
                m_injector.injectProtonDll(game, path);
                return;
            }
        }
        m_lastError = QStringLiteral("Proton target '%1' not running").arg(wanted);
        m_jobQueue.removeFirst();
        startNextJob();
        return;
    }

    // native: resolve the pid by process name at fire time
    const QString wanted = m_targetKey.mid(QStringLiteral("native:").size());
    const qint64 pid = Injector::findPid(wanted);
    if (pid > 0) {
        m_injector.injectAny(pid, path);
        return;
    }
    m_lastError = QStringLiteral("process '%1' not found").arg(wanted);
    m_jobQueue.removeFirst();
    startNextJob();
}

void AdvancedWindow::onJobFinished(bool ok)
{
    if (!m_queueBusy)
        return; // stale signal from an earlier manual injection
    if (ok)
        ++m_injectedCount;
    else if (!m_lastError.isEmpty())
        m_lastError += QStringLiteral(" (job failed)");
    else
        m_lastError = QStringLiteral("job failed");
    if (!m_jobQueue.isEmpty())
        m_jobQueue.removeFirst();
    startNextJob();
}

void AdvancedWindow::onAutoToggled(bool on)
{
    m_delaySpin->setEnabled(on);
    saveSettings();
    m_delayLeft = 0;
    m_injectedThisAppearance = false;
    if (on)
        m_statusLabel->setText(QStringLiteral("Watching for the target..."));
}

void AdvancedWindow::autoTick()
{
    if (!m_autoBox->isChecked() || m_queueBusy)
        return;

    const QString key = targetKey();
    bool present = false;
    if (key.startsWith(QStringLiteral("proton:"))) {
        const QString wanted = key.mid(QStringLiteral("proton:").size());
        for (const auto &game : m_injector.listProtonProcesses())
            if (game.gameExe.compare(wanted, Qt::CaseInsensitive) == 0) { present = true; break; }
    } else if (key.startsWith(QStringLiteral("native:"))) {
        present = Injector::findPid(key.mid(QStringLiteral("native:").size())) > 0;
    }

    if (!present) {
        // target gone: re-arm so the next appearance fires again
        if (m_delayLeft > 0 || m_injectedThisAppearance) {
            m_delayLeft = 0;
            m_injectedThisAppearance = false;
            m_statusLabel->setText(QStringLiteral("Watching for the target..."));
        }
        return;
    }

    if (m_injectedThisAppearance)
        return; // already fired for this appearance

    if (m_delayLeft == 0)
        m_delayLeft = m_delaySpin->value();

    if (m_delayLeft > 0) {
        --m_delayLeft;
        m_statusLabel->setText(QStringLiteral("Target found - injecting in %1s...").arg(m_delayLeft));
        return;
    }

    m_injectedThisAppearance = true;
    m_statusLabel->setText(QStringLiteral("Injecting..."));
    injectNow();
}

void AdvancedWindow::saveSettings()
{
    QSettings settings(QStringLiteral("Neversnooze"), QStringLiteral("Loader"));
    QStringList entries;
    for (int i = 0; i < m_dllList->count(); ++i) {
        const auto *item = m_dllList->item(i);
        entries.append(QStringLiteral("%1|%2").arg(item->checkState() == Qt::Checked ? 1 : 0).arg(item->data(Qt::UserRole).toString()));
    }
    settings.setValue(QStringLiteral("adv/entries"), entries);
    settings.setValue(QStringLiteral("adv/auto"), m_autoBox->isChecked());
    settings.setValue(QStringLiteral("adv/delay"), m_delaySpin->value());
}

void AdvancedWindow::loadSettings()
{
    QSettings settings(QStringLiteral("Neversnooze"), QStringLiteral("Loader"));
    const QStringList entries = settings.value(QStringLiteral("adv/entries")).toStringList();
    for (const QString &entry : entries) {
        const int sep = entry.indexOf(QLatin1Char('|'));
        if (sep <= 0)
            continue;
        const bool enabled = entry.left(1) == QStringLiteral("1");
        const QString path = entry.mid(sep + 1);
        auto *item = new QListWidgetItem(QFileInfo(path).fileName(), m_dllList);
        item->setData(Qt::UserRole, path);
        item->setToolTip(path);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(enabled ? Qt::Checked : Qt::Unchecked);
    }
    m_autoBox->setChecked(settings.value(QStringLiteral("adv/auto"), false).toBool());
    m_delaySpin->setValue(settings.value(QStringLiteral("adv/delay"), 0).toInt());
}

void AdvancedWindow::closeEvent(QCloseEvent *event)
{
    saveSettings();
    QDialog::closeEvent(event);
}
