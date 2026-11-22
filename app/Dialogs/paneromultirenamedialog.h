/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef PANEROMULTIRENAMEDIALOG_H
#define PANEROMULTIRENAMEDIALOG_H

#include "paneromultirenamerules.h"

#include <QDialog>
#include <QPointer>
#include <QUrl>

#include <functional>

class KJob;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QTimer;

/**
 * Multi-Rename (Ctrl+M, D-019): renames the selected files of one folder with
 * Total Commander's pattern rules (PaneroMultiRename), with a live preview,
 * conflict checks, presets and undo of the last batch. Renames run as KIO
 * jobs (never in the GUI thread, also on cloud and remote folders), in an
 * order that is safe for swapped names.
 */
class PaneroMultiRenameDialog : public QDialog
{
    Q_OBJECT
public:
    struct Entry {
        QString name;
        bool isDir = false;
        QDateTime modified;
        qint64 size = 0;
    };
    /// @p otherNames: names in @p folder that are not renamed (conflicts).
    PaneroMultiRenameDialog(QWidget *parent, const QUrl &folder, const QList<Entry> &entries, const QSet<QString> &otherNames);
    ~PaneroMultiRenameDialog() override;

    PaneroMultiRename::Options options() const;
    void setOptions(const PaneroMultiRename::Options &options);

signals:
    /// Files in @p folder were renamed (or the renames undone).
    void renamed(const QUrl &folder);

protected:
    void reject() override;

private:
    void buildUi();
    void schedulePreview();
    void updatePreview();
    void insertToken(const QString &token, int selectFrom = -1, int selectLength = 0);
    void start();
    void undo();
    void loadPresets();
    void savePreset();
    void deletePreset();
    void showHelp();
    void setBusy(bool busy);
    void updateUndoButton();
    using Steps = QList<QPair<QString, QString>>;
    /// Runs the renames one by one; on an error the finished ones are undone.
    void runSteps(const Steps &steps, const std::function<void(const QString &error)> &done);
    void runNext();

    QUrl m_folder;
    QList<Entry> m_entries;
    QSet<QString> m_otherNames;
    QStringList m_newNames;
    bool m_caseInsensitive = false;

    QLineEdit *m_nameMask = nullptr;
    QLineEdit *m_extMask = nullptr;
    QPointer<QLineEdit> m_lastMask;
    QLineEdit *m_search = nullptr;
    QLineEdit *m_replace = nullptr;
    QCheckBox *m_caseSensitive = nullptr;
    QCheckBox *m_firstOnly = nullptr;
    QCheckBox *m_inExtension = nullptr;
    QCheckBox *m_regex = nullptr;
    QComboBox *m_nameCase = nullptr;
    QComboBox *m_extCase = nullptr;
    QSpinBox *m_counterStart = nullptr;
    QSpinBox *m_counterStep = nullptr;
    QSpinBox *m_counterDigits = nullptr;
    QComboBox *m_presets = nullptr;
    QPushButton *m_deletePreset = nullptr;
    QTableWidget *m_table = nullptr;
    QLabel *m_status = nullptr;
    QPushButton *m_start = nullptr;
    QPushButton *m_undo = nullptr;
    QPushButton *m_krename = nullptr;
    QPushButton *m_close = nullptr;
    QWidget *m_options = nullptr;
    QTimer *m_previewTimer = nullptr;

    // running renames
    Steps m_steps;
    Steps m_done;
    int m_next = 0;
    bool m_cancel = false;
    bool m_rollingBack = false;
    QString m_error;
    std::function<void(const QString &)> m_finished;
    QPointer<KJob> m_job;
};

#endif // PANEROMULTIRENAMEDIALOG_H
