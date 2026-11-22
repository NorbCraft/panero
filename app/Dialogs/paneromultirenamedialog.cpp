/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "paneromultirenamedialog.h"

#include "../icon.h"
#include "../krglobal.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPushButton>
#include <QRandomGenerator>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QProcess>

#include <KConfigCore/KConfigGroup>
#include <KConfigCore/KSharedConfig>
#include <KCoreAddons/KFormat>
#include <KI18n/KLocalizedString>
#include <KIO/SimpleJob>
#include <KWidgetsAddons/KMessageBox>

using namespace PaneroMultiRename;

namespace
{
const char *const GROUP = "Panero Multi-Rename";
const char *const PRESETS_GROUP = "Panero Multi-Rename Presets";

QUrl childUrl(const QUrl &folder, const QString &name)
{
    QUrl u = folder;
    u.setPath(u.path().endsWith(QLatin1Char('/')) ? u.path() + name : u.path() + QLatin1Char('/') + name);
    return u;
}

QString problemText(Problem p)
{
    switch (p) {
    case Problem::Empty:
        return i18n("The new name is empty.");
    case Problem::Duplicate:
        return i18n("Another file gets the same name.");
    case Problem::Exists:
        return i18n("A file with this name already exists in the folder.");
    case Problem::InvalidCharacter:
        return i18n("The name contains a character that is not allowed in file names.");
    case Problem::None:
        break;
    }
    return QString();
}
}

PaneroMultiRenameDialog::PaneroMultiRenameDialog(QWidget *parent, const QUrl &folder, const QList<Entry> &entries, const QSet<QString> &otherNames)
    : QDialog(parent), m_folder(folder), m_entries(entries), m_otherNames(otherNames)
{
#ifdef Q_OS_WIN
    m_caseInsensitive = true;
#endif
    setWindowTitle(i18np("Multi-Rename: 1 item", "Multi-Rename: %1 items", entries.size()));
    setAttribute(Qt::WA_DeleteOnClose);
    m_previewTimer = new QTimer(this);
    m_previewTimer->setSingleShot(true);
    m_previewTimer->setInterval(60);
    connect(m_previewTimer, &QTimer::timeout, this, &PaneroMultiRenameDialog::updatePreview);
    buildUi();
    loadPresets();
    updateUndoButton();
    updatePreview();
    resize(960, 640);
}

PaneroMultiRenameDialog::~PaneroMultiRenameDialog() = default;

void PaneroMultiRenameDialog::buildUi()
{
    auto *top = new QVBoxLayout(this);
    m_options = new QWidget(this);
    auto *grid = new QGridLayout(m_options);
    grid->setContentsMargins(0, 0, 0, 0);

    auto tokenButton = [this](QWidget *parent, const QString &text, const QString &tip, const std::function<void()> &insert) {
        auto *b = new QToolButton(parent);
        b->setText(text);
        b->setToolTip(tip);
        b->setAutoRaise(false);
        connect(b, &QToolButton::clicked, this, insert);
        return b;
    };

    // name pattern
    auto *nameBox = new QGroupBox(i18n("Name pattern"), m_options);
    auto *nameLayout = new QVBoxLayout(nameBox);
    m_nameMask = new QLineEdit(QStringLiteral("[N]"), nameBox);
    m_nameMask->setClearButtonEnabled(true);
    nameLayout->addWidget(m_nameMask);
    auto *nameTokens = new QGridLayout;
    nameTokens->addWidget(tokenButton(nameBox, i18n("[N] Name"), i18n("The old name without the extension"), [this]() { insertToken(QStringLiteral("[N]")); }), 0, 0);
    nameTokens->addWidget(tokenButton(nameBox, i18n("[N#-#] Part"), i18n("Characters from the old name, e.g. [N1-5] or [N3-] (from the 3rd)"),
                                      [this]() { insertToken(QStringLiteral("[N1-3]"), 2, 3); }), 0, 1);
    nameTokens->addWidget(tokenButton(nameBox, i18n("[C] Counter"), i18n("A running number, see Counter"), [this]() { insertToken(QStringLiteral("[C]")); }), 0, 2);
    nameTokens->addWidget(tokenButton(nameBox, i18n("[YMD] Date"), i18n("Date of the last change, e.g. 20260803"), [this]() { insertToken(QStringLiteral("[YMD]")); }), 1, 0);
    nameTokens->addWidget(tokenButton(nameBox, i18n("[hms] Time"), i18n("Time of the last change, e.g. 142210"), [this]() { insertToken(QStringLiteral("[hms]")); }), 1, 1);
    nameTokens->addWidget(tokenButton(nameBox, i18n("[P] Folder"), i18n("Name of the folder the files are in ([G]: the folder above)"), [this]() { insertToken(QStringLiteral("[P]")); }), 1, 2);
    nameLayout->addLayout(nameTokens);
    nameLayout->addStretch();
    grid->addWidget(nameBox, 0, 0, 2, 1);

    // extension pattern
    auto *extBox = new QGroupBox(i18n("Extension"), m_options);
    auto *extLayout = new QVBoxLayout(extBox);
    m_extMask = new QLineEdit(QStringLiteral("[E]"), extBox);
    m_extMask->setClearButtonEnabled(true);
    extLayout->addWidget(m_extMask);
    auto *extTokens = new QHBoxLayout;
    extTokens->addWidget(tokenButton(extBox, i18n("[E] Ext."), i18n("The old extension"), [this]() { insertToken(QStringLiteral("[E]")); }));
    extTokens->addWidget(tokenButton(extBox, i18n("[C]"), i18n("A running number"), [this]() { insertToken(QStringLiteral("[C]")); }));
    extLayout->addLayout(extTokens);
    extLayout->addStretch();
    grid->addWidget(extBox, 0, 1, 2, 1);
    m_lastMask = m_nameMask;
    connect(m_nameMask, &QLineEdit::cursorPositionChanged, this, [this]() { m_lastMask = m_nameMask; });
    connect(m_extMask, &QLineEdit::cursorPositionChanged, this, [this]() { m_lastMask = m_extMask; });

    // search & replace
    auto *searchBox = new QGroupBox(i18n("Search && replace"), m_options);
    auto *searchLayout = new QGridLayout(searchBox);
    m_search = new QLineEdit(searchBox);
    m_search->setPlaceholderText(i18n("e.g. IMG_|DSC_"));
    m_search->setToolTip(i18n("Several terms separated by |; with regular expressions: groups for $1, $2 ..."));
    m_replace = new QLineEdit(searchBox);
    m_replace->setToolTip(i18n("One replacement for all terms, or one per term separated by |"));
    searchLayout->addWidget(new QLabel(i18n("Search for:"), searchBox), 0, 0);
    searchLayout->addWidget(m_search, 0, 1, 1, 4);
    searchLayout->addWidget(new QLabel(i18n("Replace with:"), searchBox), 1, 0);
    searchLayout->addWidget(m_replace, 1, 1, 1, 4);
    m_caseSensitive = new QCheckBox(i18n("Aa"), searchBox);
    m_caseSensitive->setToolTip(i18n("Upper and lower case must match"));
    m_firstOnly = new QCheckBox(i18n("1×"), searchBox);
    m_firstOnly->setToolTip(i18n("Replace only the first match"));
    m_inExtension = new QCheckBox(i18n("[E]"), searchBox);
    m_inExtension->setToolTip(i18n("Search in the extension too"));
    m_regex = new QCheckBox(i18n("RegEx"), searchBox);
    m_regex->setToolTip(i18n("Regular expressions"));
    searchLayout->addWidget(m_caseSensitive, 2, 1);
    searchLayout->addWidget(m_firstOnly, 2, 2);
    searchLayout->addWidget(m_inExtension, 2, 3);
    searchLayout->addWidget(m_regex, 2, 4);
    searchLayout->setRowStretch(3, 1);
    grid->addWidget(searchBox, 0, 2, 2, 1);

    // case and counter
    auto *rightColumn = new QVBoxLayout;
    auto *caseBox = new QGroupBox(i18n("Upper/lower case"), m_options);
    auto *caseLayout = new QGridLayout(caseBox);
    auto fillCase = [caseBox](QComboBox *c) {
        c->addItem(i18n("Unchanged"), int(Case::Unchanged));
        c->addItem(i18n("lower case"), int(Case::Lower));
        c->addItem(i18n("UPPER CASE"), int(Case::Upper));
        c->addItem(i18n("First letter upper"), int(Case::FirstUpper));
        c->addItem(i18n("Each Word Upper"), int(Case::WordsUpper));
        Q_UNUSED(caseBox)
    };
    m_nameCase = new QComboBox(caseBox);
    fillCase(m_nameCase);
    m_extCase = new QComboBox(caseBox);
    fillCase(m_extCase);
    caseLayout->addWidget(new QLabel(i18n("Name:"), caseBox), 0, 0);
    caseLayout->addWidget(m_nameCase, 0, 1);
    caseLayout->addWidget(new QLabel(i18n("Extension:"), caseBox), 1, 0);
    caseLayout->addWidget(m_extCase, 1, 1);
    rightColumn->addWidget(caseBox);
    auto *counterBox = new QGroupBox(i18n("Counter [C]"), m_options);
    auto *counterLayout = new QGridLayout(counterBox);
    m_counterStart = new QSpinBox(counterBox);
    m_counterStart->setRange(-999999, 999999);
    m_counterStart->setValue(1);
    m_counterStep = new QSpinBox(counterBox);
    m_counterStep->setRange(-9999, 9999);
    m_counterStep->setValue(1);
    m_counterDigits = new QSpinBox(counterBox);
    m_counterDigits->setRange(1, 9);
    m_counterDigits->setValue(1);
    counterLayout->addWidget(new QLabel(i18n("Start at:"), counterBox), 0, 0);
    counterLayout->addWidget(m_counterStart, 0, 1);
    counterLayout->addWidget(new QLabel(i18n("Step by:"), counterBox), 1, 0);
    counterLayout->addWidget(m_counterStep, 1, 1);
    counterLayout->addWidget(new QLabel(i18n("Digits:"), counterBox), 2, 0);
    counterLayout->addWidget(m_counterDigits, 2, 1);
    rightColumn->addWidget(counterBox);
    grid->addLayout(rightColumn, 0, 3, 2, 1);
    grid->setColumnStretch(0, 3);
    grid->setColumnStretch(1, 1);
    grid->setColumnStretch(2, 3);
    grid->setColumnStretch(3, 2);
    top->addWidget(m_options);

    // presets and help
    auto *presetRow = new QHBoxLayout;
    presetRow->addWidget(new QLabel(i18n("Saved settings:"), this));
    m_presets = new QComboBox(this);
    m_presets->setMinimumContentsLength(18);
    presetRow->addWidget(m_presets);
    auto *savePreset = new QPushButton(Icon(QStringLiteral("document-save")), i18n("Save..."), this);
    m_deletePreset = new QPushButton(Icon(QStringLiteral("edit-delete")), i18n("Delete"), this);
    auto *reset = new QPushButton(Icon(QStringLiteral("edit-undo")), i18n("Reset"), this);
    reset->setToolTip(i18n("Back to [N] and [E], no search, no case change"));
    presetRow->addWidget(savePreset);
    presetRow->addWidget(m_deletePreset);
    presetRow->addWidget(reset);
    presetRow->addStretch();
    auto *help = new QPushButton(Icon(QStringLiteral("help-contents")), i18n("Pattern Help"), this);
    presetRow->addWidget(help);
    top->addLayout(presetRow);
    connect(savePreset, &QPushButton::clicked, this, &PaneroMultiRenameDialog::savePreset);
    connect(m_deletePreset, &QPushButton::clicked, this, &PaneroMultiRenameDialog::deletePreset);
    connect(reset, &QPushButton::clicked, this, [this]() { setOptions(Options()); });
    connect(help, &QPushButton::clicked, this, &PaneroMultiRenameDialog::showHelp);
    connect(m_presets, QOverload<int>::of(&QComboBox::activated), this, [this](int index) {
        const QString name = m_presets->itemData(index).toString();
        if (!name.isEmpty())
            setOptions(Options::fromString(KConfigGroup(krConfig, PRESETS_GROUP).readEntry(name, QString())));
        m_deletePreset->setEnabled(!name.isEmpty());
    });

    // preview
    m_table = new QTableWidget(0, 4, this);
    m_table->setHorizontalHeaderLabels({i18n("Old name"), i18n("New name"), i18n("Size"), i18n("Modified")});
    m_table->verticalHeader()->hide();
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setWordWrap(false);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    top->addWidget(m_table, 1);

    // status and buttons
    auto *bottom = new QHBoxLayout;
    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    bottom->addWidget(m_status, 1);
    if (!QStandardPaths::findExecutable(QStringLiteral("krename")).isEmpty()) {
        m_krename = new QPushButton(Icon(QStringLiteral("krename")), i18n("Open in KRename"), this);
        m_krename->setToolTip(i18n("KRename's extras: photo, music and PDF data, copy or move while renaming"));
        bottom->addWidget(m_krename);
        connect(m_krename, &QPushButton::clicked, this, [this]() {
            QStringList args;
            for (const Entry &e : qAsConst(m_entries))
                args << childUrl(m_folder, e.name).toString();
            if (!QProcess::startDetached(QStringLiteral("krename"), args))
                KMessageBox::error(this, i18n("KRename could not be started."));
            else
                QDialog::reject();
        });
    }
    m_undo = new QPushButton(Icon(QStringLiteral("edit-undo")), i18n("Undo Last Rename"), this);
    m_start = new QPushButton(Icon(QStringLiteral("dialog-ok-apply")), i18n("Start"), this);
    m_start->setDefault(true);
    m_close = new QPushButton(Icon(QStringLiteral("dialog-close")), i18n("Close"), this);
    bottom->addWidget(m_undo);
    bottom->addWidget(m_start);
    bottom->addWidget(m_close);
    top->addLayout(bottom);
    connect(m_start, &QPushButton::clicked, this, &PaneroMultiRenameDialog::start);
    connect(m_undo, &QPushButton::clicked, this, &PaneroMultiRenameDialog::undo);
    connect(m_close, &QPushButton::clicked, this, &PaneroMultiRenameDialog::reject);

    for (QLineEdit *e : {m_nameMask, m_extMask, m_search, m_replace})
        connect(e, &QLineEdit::textChanged, this, &PaneroMultiRenameDialog::schedulePreview);
    for (QCheckBox *c : {m_caseSensitive, m_firstOnly, m_inExtension, m_regex})
        connect(c, &QCheckBox::toggled, this, &PaneroMultiRenameDialog::schedulePreview);
    for (QComboBox *c : {m_nameCase, m_extCase})
        connect(c, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &PaneroMultiRenameDialog::schedulePreview);
    for (QSpinBox *s : {m_counterStart, m_counterStep, m_counterDigits})
        connect(s, QOverload<int>::of(&QSpinBox::valueChanged), this, &PaneroMultiRenameDialog::schedulePreview);
    m_nameMask->setFocus();
    m_nameMask->selectAll();
}

Options PaneroMultiRenameDialog::options() const
{
    Options o;
    o.nameMask = m_nameMask->text();
    o.extMask = m_extMask->text();
    o.search = m_search->text();
    o.replace = m_replace->text();
    o.regex = m_regex->isChecked();
    o.caseSensitive = m_caseSensitive->isChecked();
    o.firstOnly = m_firstOnly->isChecked();
    o.inExtension = m_inExtension->isChecked();
    o.nameCase = Case(m_nameCase->currentData().toInt());
    o.extCase = Case(m_extCase->currentData().toInt());
    o.counterStart = m_counterStart->value();
    o.counterStep = m_counterStep->value();
    o.counterDigits = m_counterDigits->value();
    return o;
}

void PaneroMultiRenameDialog::setOptions(const Options &o)
{
    m_nameMask->setText(o.nameMask);
    m_extMask->setText(o.extMask);
    m_search->setText(o.search);
    m_replace->setText(o.replace);
    m_regex->setChecked(o.regex);
    m_caseSensitive->setChecked(o.caseSensitive);
    m_firstOnly->setChecked(o.firstOnly);
    m_inExtension->setChecked(o.inExtension);
    m_nameCase->setCurrentIndex(qMax(0, m_nameCase->findData(int(o.nameCase))));
    m_extCase->setCurrentIndex(qMax(0, m_extCase->findData(int(o.extCase))));
    m_counterStart->setValue(o.counterStart);
    m_counterStep->setValue(o.counterStep);
    m_counterDigits->setValue(o.counterDigits);
    updatePreview();
}

void PaneroMultiRenameDialog::insertToken(const QString &token, int selectFrom, int selectLength)
{
    QLineEdit *edit = m_lastMask ? m_lastMask.data() : m_nameMask;
    const int at = edit->hasSelectedText() ? edit->selectionStart() : edit->cursorPosition();
    edit->insert(token);
    edit->setFocus();
    if (selectFrom >= 0)
        edit->setSelection(at + selectFrom, selectLength);
}

void PaneroMultiRenameDialog::schedulePreview()
{
    m_previewTimer->start();
}

void PaneroMultiRenameDialog::updatePreview()
{
    const Options o = options();
    QString regexError;
    const bool searchOk = searchIsValid(o, &regexError);
    QStringList oldNames;
    m_newNames.clear();
    const QString parent = m_folder.adjusted(QUrl::StripTrailingSlash).fileName();
    const QString grandParent = m_folder.adjusted(QUrl::StripTrailingSlash | QUrl::RemoveFilename).adjusted(QUrl::StripTrailingSlash).fileName();
    for (int i = 0; i < m_entries.size(); ++i) {
        const Entry &e = m_entries.at(i);
        Item item;
        item.fileName = e.name;
        item.isDir = e.isDir;
        item.modified = e.modified;
        item.parentName = parent;
        item.grandParentName = grandParent;
        oldNames << e.name;
        m_newNames << newName(item, o, i);
    }
    const QList<Problem> problems = check(oldNames, m_newNames, m_otherNames, m_caseInsensitive);

    m_table->setUpdatesEnabled(false);
    m_table->setRowCount(m_entries.size());
    int changed = 0, bad = 0;
    const QColor badColor(0xd0, 0x3b, 0x3b); // readable on light and dark backgrounds
    for (int i = 0; i < m_entries.size(); ++i) {
        const Entry &e = m_entries.at(i);
        auto set = [this, i](int column, const QString &text) {
            QTableWidgetItem *item = m_table->item(i, column);
            if (!item) {
                item = new QTableWidgetItem;
                m_table->setItem(i, column, item);
            }
            item->setText(text);
            return item;
        };
        set(0, e.name)->setIcon(Icon(e.isDir ? QStringLiteral("folder") : QStringLiteral("text-x-generic")));
        QTableWidgetItem *newItem = set(1, m_newNames.at(i));
        set(2, e.isDir ? i18n("<DIR>") : KFormat().formatByteSize(double(e.size)))->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        set(3, QLocale().toString(e.modified, QLocale::ShortFormat));
        const Problem p = problems.at(i);
        newItem->setForeground(p == Problem::None ? palette().color(QPalette::Text) : badColor);
        newItem->setToolTip(problemText(p));
        if (p != Problem::None)
            ++bad;
        else if (m_newNames.at(i) != e.name)
            ++changed;
    }
    m_table->setUpdatesEnabled(true);

    if (!searchOk) {
        m_status->setText(i18n("The search pattern is not a valid regular expression: %1", regexError));
    } else if (bad > 0) {
        m_status->setText(i18np("1 name has a problem (red, see its tooltip).", "%1 names have a problem (red, see their tooltips).", bad));
    } else if (changed == 0) {
        m_status->setText(i18n("No name changes yet. Type a pattern or use the buttons above."));
    } else {
        m_status->setText(i18np("1 item will be renamed.", "%1 items will be renamed.", changed));
    }
    m_start->setEnabled(searchOk && bad == 0 && changed > 0 && m_steps.isEmpty());
}

void PaneroMultiRenameDialog::start()
{
    updatePreview();
    if (!m_start->isEnabled())
        return;
    QStringList oldNames, newNames;
    for (int i = 0; i < m_entries.size(); ++i) {
        oldNames << m_entries.at(i).name;
        newNames << m_newNames.at(i);
    }
    const QString temp = QStringLiteral(".panero-rename-%1-").arg(QRandomGenerator::global()->generate(), 8, 16, QLatin1Char('0'));
    const Steps steps = plan(oldNames, newNames, temp, m_caseInsensitive);
    setBusy(true);
    runSteps(steps, [this, oldNames, newNames](const QString &error) {
        setBusy(false);
        emit renamed(m_folder);
        if (!error.isEmpty()) {
            KMessageBox::error(this, i18n("Renaming stopped: %1\nThe files renamed so far were given their old names back.", error));
            return;
        }
        // remember for Undo (also after the dialog is closed)
        KConfigGroup g(krConfig, GROUP);
        g.writeEntry("Undo Folder", m_folder.toString());
        g.writeEntry("Undo Old Names", oldNames);
        g.writeEntry("Undo New Names", newNames);
        g.sync();
        // the files have their new names now: a second Start works from these
        for (int i = 0; i < m_entries.size(); ++i)
            m_entries[i].name = newNames.at(i);
        setOptions(Options());
        updateUndoButton();
        m_status->setText(i18np("1 item renamed.", "%1 items renamed.", newNames.size()));
    });
}

void PaneroMultiRenameDialog::undo()
{
    KConfigGroup g(krConfig, GROUP);
    const QUrl folder(g.readEntry("Undo Folder", QString()));
    const QStringList oldNames = g.readEntry("Undo Old Names", QStringList());
    const QStringList newNames = g.readEntry("Undo New Names", QStringList());
    if (!folder.isValid() || oldNames.size() != newNames.size() || oldNames.isEmpty())
        return;
    const QString temp = QStringLiteral(".panero-rename-%1-").arg(QRandomGenerator::global()->generate(), 8, 16, QLatin1Char('0'));
    const QUrl previousFolder = m_folder;
    m_folder = folder;
    setBusy(true);
    runSteps(plan(newNames, oldNames, temp, m_caseInsensitive), [this, folder, previousFolder, oldNames, newNames](const QString &error) {
        setBusy(false);
        m_folder = previousFolder;
        emit renamed(folder);
        if (!error.isEmpty()) {
            KMessageBox::error(this, i18n("Undo stopped: %1", error));
            return;
        }
        KConfigGroup group(krConfig, GROUP);
        group.deleteEntry("Undo Folder");
        group.deleteEntry("Undo Old Names");
        group.deleteEntry("Undo New Names");
        group.sync();
        if (folder.matches(m_folder, QUrl::StripTrailingSlash)) {
            for (Entry &e : m_entries) {
                const int i = newNames.indexOf(e.name);
                if (i >= 0)
                    e.name = oldNames.at(i);
            }
        }
        updateUndoButton();
        updatePreview();
        m_status->setText(i18n("The last rename was undone."));
    });
}

void PaneroMultiRenameDialog::runSteps(const Steps &steps, const std::function<void(const QString &)> &done)
{
    m_steps = steps;
    m_done.clear();
    m_next = 0;
    m_cancel = false;
    m_rollingBack = false;
    m_error.clear();
    m_finished = done;
    runNext();
}

void PaneroMultiRenameDialog::runNext()
{
    if (!m_rollingBack && (m_cancel || !m_error.isEmpty()) && !m_done.isEmpty()) {
        // put back what was renamed so far, newest first
        if (m_error.isEmpty())
            m_error = i18n("Cancelled.");
        Steps back;
        for (int i = m_done.size() - 1; i >= 0; --i)
            back << qMakePair(m_done.at(i).second, m_done.at(i).first);
        m_steps = back;
        m_next = 0;
        m_rollingBack = true;
    }
    if (m_next >= m_steps.size() || (!m_rollingBack && (m_cancel || !m_error.isEmpty()))) {
        const QString error = m_cancel && m_error.isEmpty() ? i18n("Cancelled.") : m_error;
        m_steps.clear();
        auto finished = m_finished;
        m_finished = nullptr;
        if (finished)
            finished(error);
        return;
    }
    const auto step = m_steps.at(m_next++);
    m_status->setText(m_rollingBack ? i18n("Restoring the old names...")
                                    : i18n("Renaming %1 of %2...", m_next, m_steps.size()));
    KIO::SimpleJob *job = KIO::rename(childUrl(m_folder, step.first), childUrl(m_folder, step.second), KIO::HideProgressInfo);
    m_job = job;
    connect(job, &KJob::result, this, [this, step](KJob *j) {
        if (j->error()) {
            if (!m_rollingBack)
                m_error = j->errorString();
        } else if (!m_rollingBack) {
            m_done << step;
        }
        runNext();
    });
}

void PaneroMultiRenameDialog::setBusy(bool busy)
{
    m_options->setEnabled(!busy);
    m_presets->setEnabled(!busy);
    m_start->setEnabled(!busy);
    m_undo->setEnabled(!busy);
    if (m_krename)
        m_krename->setEnabled(!busy);
    m_close->setText(busy ? i18n("Cancel") : i18n("Close"));
    if (!busy)
        updatePreview();
}

void PaneroMultiRenameDialog::reject()
{
    if (!m_steps.isEmpty()) {
        m_cancel = true; // stops after the running rename and restores the old names
        return;
    }
    QDialog::reject();
}

void PaneroMultiRenameDialog::updateUndoButton()
{
    const KConfigGroup g(krConfig, GROUP);
    const QStringList names = g.readEntry("Undo New Names", QStringList());
    m_undo->setEnabled(!names.isEmpty());
    m_undo->setToolTip(names.isEmpty() ? i18n("Nothing to undo")
                                       : i18np("Gives 1 item in %2 its old name back", "Gives %1 items in %2 their old names back", names.size(),
                                               QUrl(g.readEntry("Undo Folder", QString())).toDisplayString(QUrl::PreferLocalFile)));
}

void PaneroMultiRenameDialog::loadPresets()
{
    m_presets->clear();
    m_presets->addItem(i18n("(none)"), QString());
    QStringList names = KConfigGroup(krConfig, PRESETS_GROUP).keyList();
    names.sort(Qt::CaseInsensitive);
    for (const QString &n : names)
        m_presets->addItem(n, n);
    m_deletePreset->setEnabled(false);
}

void PaneroMultiRenameDialog::savePreset()
{
    bool ok = false;
    const QString current = m_presets->currentData().toString();
    const QString name = QInputDialog::getText(this, i18n("Save Settings"), i18n("Name for these settings:"), QLineEdit::Normal, current, &ok).trimmed();
    if (!ok || name.isEmpty())
        return;
    KConfigGroup g(krConfig, PRESETS_GROUP);
    g.writeEntry(name, options().toString());
    g.sync();
    loadPresets();
    m_presets->setCurrentIndex(qMax(0, m_presets->findData(name)));
    m_deletePreset->setEnabled(true);
}

void PaneroMultiRenameDialog::deletePreset()
{
    const QString name = m_presets->currentData().toString();
    if (name.isEmpty())
        return;
    KConfigGroup g(krConfig, PRESETS_GROUP);
    g.deleteEntry(name);
    g.sync();
    loadPresets();
}

void PaneroMultiRenameDialog::showHelp()
{
    const QString rows = QStringLiteral(
        "<tr><td><b>[N]</b></td><td>%1</td></tr><tr><td><b>[N3]</b></td><td>%2</td></tr>"
        "<tr><td><b>[N2-5]</b></td><td>%3</td></tr><tr><td><b>[N2,3]</b></td><td>%4</td></tr>"
        "<tr><td><b>[N2-]</b></td><td>%5</td></tr><tr><td><b>[N-3-]</b></td><td>%6</td></tr>"
        "<tr><td><b>[E]</b></td><td>%7</td></tr><tr><td><b>[C]</b></td><td>%8</td></tr>"
        "<tr><td><b>[C10+5:3]</b></td><td>%9</td></tr><tr><td><b>[P] [G]</b></td><td>%10</td></tr>"
        "<tr><td><b>[YMD] [hms]</b></td><td>%11</td></tr><tr><td><b>[Y] [y] [M] [D] [h] [n] [s]</b></td><td>%12</td></tr>"
        "<tr><td><b>[[] []]</b></td><td>%13</td></tr>")
        .arg(i18n("old name without extension"), i18n("3rd character of the name"), i18n("characters 2 to 5"),
             i18n("3 characters from the 2nd"), i18n("from the 2nd character to the end"), i18n("the last 3 characters"),
             i18n("old extension (the same parts as for [N] work: [E1-2] ...)"), i18n("counter (start, step and digits on the right)"),
             i18n("counter starting at 10, step 5, 3 digits"), i18n("name of the folder / the folder above"),
             i18n("date / time of the last change"), i18n("year (4 or 2 digits), month, day, hour, minute, second"),
             i18n("a literal [ or ]"));
    KMessageBox::information(this,
                             QStringLiteral("<qt><table cellspacing=\"4\">%1</table><p>%2</p><p>%3</p></qt>")
                                 .arg(rows,
                                      i18n("Search &amp; replace: several terms with |, e.g. <b>IMG_|DSC_</b>. With RegEx, "
                                           "<b>(\\d+)</b> in the search and <b>$1</b> in the replacement."),
                                      i18n("Order: pattern, then search &amp; replace, then upper/lower case.")),
                             i18n("Multi-Rename Patterns"));
}
