/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef PANEROMULTIRENAMERULES_H
#define PANEROMULTIRENAMERULES_H

#include <QDateTime>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

/**
 * The rules of Panero's Multi-Rename (D-019), following Total Commander's
 * pattern syntax. Pure functions: no file access, so the preview is instant
 * and the rules are unit-tested.
 *
 * Name and extension patterns:
 *   [N]        name without extension      [E]        extension
 *   [N3]       3rd character               [N2-5]     characters 2 to 5
 *   [N2,3]     3 characters from the 2nd   [N2-]      from the 2nd to the end
 *   [N-3-]     the last 3 characters       (the same forms work for [E])
 *   [C]        counter (start, step, digits from the counter fields)
 *   [C10+5:3]  counter starting at 10, step 5, 3 digits
 *   [P] / [G]  parent / grandparent folder name
 *   [YMD] [hms] date / time of the last change, [Y] [y] [M] [D] [h] [n] [s]
 *              year (4 / 2 digits), month, day, hour, minute, second
 *   [[] / []]  a literal "[" / "]"
 * Then search & replace (several with "|", optionally regular expressions
 * with $1...), then upper/lower case for name and extension.
 */
namespace PaneroMultiRename
{
enum class Case { Unchanged, Lower, Upper, FirstUpper, WordsUpper };

struct Options {
    QString nameMask = QStringLiteral("[N]");
    QString extMask = QStringLiteral("[E]");
    QString search;
    QString replace;
    bool regex = false;
    bool caseSensitive = false;
    bool firstOnly = false;   ///< replace only the first match
    bool inExtension = false; ///< search & replace also in the extension
    Case nameCase = Case::Unchanged;
    Case extCase = Case::Unchanged;
    int counterStart = 1;
    int counterStep = 1;
    int counterDigits = 1;

    QString toString() const;                    ///< for presets
    static Options fromString(const QString &s); ///< tolerant of missing keys
};

struct Item {
    QString fileName; ///< current name with extension
    bool isDir = false;
    QDateTime modified;
    QString parentName;
    QString grandParentName;
};

/// Splits "archive.tar.gz" into "archive.tar" + "gz"; folders and names
/// like ".bashrc" have no extension.
void splitName(const QString &fileName, bool isDir, QString *name, QString *ext);

/// The new name of @p item, the @p index-th file of the batch (counter).
QString newName(const Item &item, const Options &options, int index);

/// True when the search pattern is a valid regular expression (or not regex).
bool searchIsValid(const Options &options, QString *error = nullptr);

enum class Problem { None, Empty, Duplicate, Exists, InvalidCharacter };

/// Problems of a batch: @p newNames[i] is the new name of @p oldNames[i];
/// @p otherNames are names in the folder that are not renamed.
/// @p caseInsensitive: the file system does not tell upper from lower case.
QList<Problem> check(const QStringList &oldNames, const QStringList &newNames, const QSet<QString> &otherNames, bool caseInsensitive);

/// Order of renames, safe for swaps and cycles (a->b, b->a): pairs (from,
/// to) of names; a name may first go to a temporary name.
QList<QPair<QString, QString>> plan(const QStringList &oldNames, const QStringList &newNames, const QString &tempPrefix, bool caseInsensitive);
}

#endif // PANEROMULTIRENAMERULES_H
