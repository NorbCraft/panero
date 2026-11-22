/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "paneromultirenamerules.h"

#include <QHash>
#include <QRegularExpression>
#include <QJsonDocument>
#include <QJsonObject>

namespace PaneroMultiRename
{
namespace
{
QString pad(qint64 value, int digits)
{
    const QString s = QString::number(qAbs(value)).rightJustified(qMax(1, digits), QLatin1Char('0'));
    return value < 0 ? QLatin1Char('-') + s : s;
}

/// The part of @p text a range spec selects ("", "3", "2-5", "2,3", "2-", "-3-").
QString part(const QString &text, const QString &spec)
{
    if (spec.isEmpty())
        return text;
    static const QRegularExpression single(QStringLiteral("^(\\d+)$"));
    static const QRegularExpression range(QStringLiteral("^(\\d+)-(\\d+)$"));
    static const QRegularExpression count(QStringLiteral("^(\\d+),(\\d+)$"));
    static const QRegularExpression toEnd(QStringLiteral("^(\\d+)-$"));
    static const QRegularExpression last(QStringLiteral("^-(\\d+)-$"));
    QRegularExpressionMatch m;
    if ((m = single.match(spec)).hasMatch())
        return text.mid(m.captured(1).toInt() - 1, 1);
    if ((m = range.match(spec)).hasMatch()) {
        const int from = qMax(1, m.captured(1).toInt()), to = m.captured(2).toInt();
        return to < from ? QString() : text.mid(from - 1, to - from + 1);
    }
    if ((m = count.match(spec)).hasMatch())
        return text.mid(qMax(1, m.captured(1).toInt()) - 1, m.captured(2).toInt());
    if ((m = toEnd.match(spec)).hasMatch())
        return text.mid(qMax(1, m.captured(1).toInt()) - 1);
    if ((m = last.match(spec)).hasMatch())
        return text.right(m.captured(1).toInt());
    return QString(); // unknown form: nothing (the preview shows it)
}

QString expand(const QString &mask, const Item &item, const QString &name, const QString &ext, const Options &o, int index)
{
    // "[[]" and "[]]" are literal brackets; tokens hold no brackets inside
    static const QRegularExpression token(QStringLiteral("\\[\\[\\]|\\[\\]\\]|\\[(N|E|C|P|G|YMD|hms|Y|y|M|D|h|n|s)([^\\[\\]]*)\\]"));
    QString out;
    int pos = 0;
    auto it = token.globalMatch(mask);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        out += mask.mid(pos, m.capturedStart() - pos);
        pos = m.capturedEnd();
        const QString t = m.captured(1), spec = m.captured(2);
        const QDateTime &d = item.modified;
        if (m.captured(0) == QLatin1String("[[]"))
            out += QLatin1Char('[');
        else if (m.captured(0) == QLatin1String("[]]"))
            out += QLatin1Char(']');
        else if (t == QLatin1String("N"))
            out += part(name, spec);
        else if (t == QLatin1String("E"))
            out += part(ext, spec);
        else if (t == QLatin1String("C")) {
            int start = o.counterStart, step = o.counterStep, digits = o.counterDigits;
            // [C10+5:3]: start 10, step 5, 3 digits (each part optional)
            static const QRegularExpression cspec(QStringLiteral("^(-?\\d+)?(?:\\+(-?\\d+))?(?::(\\d+))?$"));
            const QRegularExpressionMatch c = cspec.match(spec);
            if (c.hasMatch()) {
                if (!c.captured(1).isEmpty())
                    start = c.captured(1).toInt();
                if (!c.captured(2).isEmpty())
                    step = c.captured(2).toInt();
                if (!c.captured(3).isEmpty())
                    digits = c.captured(3).toInt();
            }
            out += pad(qint64(start) + qint64(step) * index, digits);
        } else if (t == QLatin1String("P"))
            out += part(item.parentName, spec);
        else if (t == QLatin1String("G"))
            out += part(item.grandParentName, spec);
        else if (!d.isValid())
            continue;
        else if (t == QLatin1String("YMD"))
            out += d.toString(QStringLiteral("yyyyMMdd"));
        else if (t == QLatin1String("hms"))
            out += d.toString(QStringLiteral("HHmmss"));
        else if (t == QLatin1String("Y"))
            out += d.toString(QStringLiteral("yyyy"));
        else if (t == QLatin1String("y"))
            out += d.toString(QStringLiteral("yy"));
        else if (t == QLatin1String("M"))
            out += d.toString(QStringLiteral("MM"));
        else if (t == QLatin1String("D"))
            out += d.toString(QStringLiteral("dd"));
        else if (t == QLatin1String("h"))
            out += d.toString(QStringLiteral("HH"));
        else if (t == QLatin1String("n"))
            out += d.toString(QStringLiteral("mm"));
        else if (t == QLatin1String("s"))
            out += d.toString(QStringLiteral("ss"));
    }
    out += mask.mid(pos);
    return out;
}

QString applyCase(const QString &s, Case c)
{
    switch (c) {
    case Case::Lower:
        return s.toLower();
    case Case::Upper:
        return s.toUpper();
    case Case::FirstUpper:
        return s.isEmpty() ? s : s.left(1).toUpper() + s.mid(1).toLower();
    case Case::WordsUpper: {
        QString r = s.toLower();
        bool start = true;
        for (QChar &ch : r) {
            if (start && ch.isLetter())
                ch = ch.toUpper();
            start = !ch.isLetterOrNumber() && ch != QLatin1Char('\'');
        }
        return r;
    }
    case Case::Unchanged:
        break;
    }
    return s;
}

/// "$1" (as typed, like Total Commander) -> "\\1" (QString::replace syntax)
QString regexReplacement(const QString &replacement)
{
    QString r;
    for (int i = 0; i < replacement.size(); ++i) {
        const QChar ch = replacement.at(i);
        if (ch == QLatin1Char('$') && i + 1 < replacement.size() && replacement.at(i + 1).isDigit()) {
            r += QLatin1Char('\\');
            continue;
        }
        r += ch;
    }
    return r;
}

QString searchReplace(const QString &text, const Options &o)
{
    if (o.search.isEmpty())
        return text;
    const QStringList finds = o.search.split(QLatin1Char('|'));
    const QStringList repls = o.replace.split(QLatin1Char('|'));
    QString s = text;
    for (int i = 0; i < finds.size(); ++i) {
        if (finds.at(i).isEmpty())
            continue;
        // one replacement for all, or one per search term
        const QString repl = repls.size() > 1 ? repls.value(i) : o.replace;
        if (!o.regex) {
            // plain text: no pattern language at all, the replacement is literal
            const Qt::CaseSensitivity cs = o.caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
            const QString &find = finds.at(i);
            QString result;
            int from = 0, at;
            while ((at = s.indexOf(find, from, cs)) >= 0) {
                result += s.mid(from, at - from) + repl;
                from = at + find.size();
                if (o.firstOnly)
                    break;
            }
            s = result + s.mid(from);
            continue;
        }
        QRegularExpression re(finds.at(i), o.caseSensitive ? QRegularExpression::NoPatternOption : QRegularExpression::CaseInsensitiveOption);
        if (!re.isValid())
            return text;
        const QString r = regexReplacement(repl);
        if (o.firstOnly) {
            const QRegularExpressionMatch m = re.match(s);
            if (m.hasMatch()) {
                // expand \1... from this match (as QString::replace would)
                QString one;
                for (int k = 0; k < r.size(); ++k) {
                    if (r.at(k) == QLatin1Char('\\') && k + 1 < r.size()) {
                        const QChar next = r.at(++k);
                        one += next.isDigit() ? m.captured(next.digitValue()) : QString(next);
                    } else {
                        one += r.at(k);
                    }
                }
                s.replace(m.capturedStart(), m.capturedLength(), one);
            }
        } else {
            s.replace(re, r);
        }
    }
    return s;
}

QString key(const QString &name, bool caseInsensitive)
{
    return caseInsensitive ? name.toLower() : name;
}
}

void splitName(const QString &fileName, bool isDir, QString *name, QString *ext)
{
    const int dot = fileName.lastIndexOf(QLatin1Char('.'));
    if (isDir || dot <= 0 || dot == fileName.size() - 1) {
        *name = fileName;
        ext->clear();
        return;
    }
    *name = fileName.left(dot);
    *ext = fileName.mid(dot + 1);
}

QString newName(const Item &item, const Options &o, int index)
{
    QString name, ext;
    splitName(item.fileName, item.isDir, &name, &ext);
    QString newBase = expand(o.nameMask, item, name, ext, o, index);
    QString newExt = expand(o.extMask, item, name, ext, o, index);
    if (o.inExtension) {
        QString whole = newExt.isEmpty() ? newBase : newBase + QLatin1Char('.') + newExt;
        whole = searchReplace(whole, o);
        splitName(whole, newExt.isEmpty(), &newBase, &newExt);
    } else {
        newBase = searchReplace(newBase, o);
    }
    newBase = applyCase(newBase, o.nameCase);
    newExt = applyCase(newExt, o.extCase);
    return newExt.isEmpty() ? newBase : newBase + QLatin1Char('.') + newExt;
}

bool searchIsValid(const Options &o, QString *error)
{
    if (!o.regex || o.search.isEmpty())
        return true;
    for (const QString &f : o.search.split(QLatin1Char('|'))) {
        const QRegularExpression re(f);
        if (!re.isValid()) {
            if (error)
                *error = re.errorString();
            return false;
        }
    }
    return true;
}

QList<Problem> check(const QStringList &oldNames, const QStringList &newNames, const QSet<QString> &otherNames, bool caseInsensitive)
{
    QHash<QString, int> count;
    for (const QString &n : newNames)
        ++count[key(n, caseInsensitive)];
    QSet<QString> others;
    for (const QString &n : otherNames)
        others.insert(key(n, caseInsensitive));
    QList<Problem> result;
    for (int i = 0; i < newNames.size(); ++i) {
        const QString &n = newNames.at(i);
        Problem p = Problem::None;
        if (n.trimmed().isEmpty() || n == QLatin1String(".") || n == QLatin1String(".."))
            p = Problem::Empty;
        else if (n.contains(QLatin1Char('/')) || n.contains(QChar(0))
#ifdef Q_OS_WIN
                 || n.contains(QRegularExpression(QStringLiteral("[<>:\"\\\\|?*]"))) || n.endsWith(QLatin1Char('.')) || n.endsWith(QLatin1Char(' '))
#endif
        )
            p = Problem::InvalidCharacter;
        else if (count.value(key(n, caseInsensitive)) > 1)
            p = Problem::Duplicate;
        else if (others.contains(key(n, caseInsensitive)) && key(n, caseInsensitive) != key(oldNames.value(i), caseInsensitive))
            p = Problem::Exists;
        result << p;
    }
    return result;
}

QList<QPair<QString, QString>> plan(const QStringList &oldNames, const QStringList &newNames, const QString &tempPrefix, bool caseInsensitive)
{
    // names taken now that another file of the batch wants: those go to a
    // temporary name first, the rest directly
    QSet<QString> oldKeys;
    for (const QString &n : oldNames)
        oldKeys.insert(key(n, caseInsensitive));
    QList<QPair<QString, QString>> direct, viaTemp, fromTemp;
    for (int i = 0; i < oldNames.size(); ++i) {
        const QString &from = oldNames.at(i), &to = newNames.at(i);
        if (from == to)
            continue;
        const bool targetIsBusy = oldKeys.contains(key(to, caseInsensitive)) && key(to, caseInsensitive) != key(from, caseInsensitive);
        // only the case changes on a case-insensitive file system: via a temporary name
        const bool caseOnly = caseInsensitive && key(to, true) == key(from, true);
        if (targetIsBusy || caseOnly) {
            const QString temp = tempPrefix + QString::number(i);
            viaTemp << qMakePair(from, temp);
            fromTemp << qMakePair(temp, to);
        } else {
            direct << qMakePair(from, to);
        }
    }
    // first free the busy names, then the direct renames, then the rest
    return viaTemp + direct + fromTemp;
}

QString Options::toString() const
{
    QJsonObject j;
    j.insert(QStringLiteral("name"), nameMask);
    j.insert(QStringLiteral("ext"), extMask);
    j.insert(QStringLiteral("search"), search);
    j.insert(QStringLiteral("replace"), replace);
    j.insert(QStringLiteral("regex"), regex);
    j.insert(QStringLiteral("case"), caseSensitive);
    j.insert(QStringLiteral("first"), firstOnly);
    j.insert(QStringLiteral("inext"), inExtension);
    j.insert(QStringLiteral("ncase"), int(nameCase));
    j.insert(QStringLiteral("ecase"), int(extCase));
    j.insert(QStringLiteral("start"), counterStart);
    j.insert(QStringLiteral("step"), counterStep);
    j.insert(QStringLiteral("digits"), counterDigits);
    return QString::fromUtf8(QJsonDocument(j).toJson(QJsonDocument::Compact));
}

Options Options::fromString(const QString &s)
{
    const QJsonObject j = QJsonDocument::fromJson(s.toUtf8()).object();
    Options o;
    o.nameMask = j.value(QStringLiteral("name")).toString(o.nameMask);
    o.extMask = j.value(QStringLiteral("ext")).toString(o.extMask);
    o.search = j.value(QStringLiteral("search")).toString();
    o.replace = j.value(QStringLiteral("replace")).toString();
    o.regex = j.value(QStringLiteral("regex")).toBool();
    o.caseSensitive = j.value(QStringLiteral("case")).toBool();
    o.firstOnly = j.value(QStringLiteral("first")).toBool();
    o.inExtension = j.value(QStringLiteral("inext")).toBool();
    o.nameCase = Case(qBound(0, j.value(QStringLiteral("ncase")).toInt(), int(Case::WordsUpper)));
    o.extCase = Case(qBound(0, j.value(QStringLiteral("ecase")).toInt(), int(Case::WordsUpper)));
    o.counterStart = j.value(QStringLiteral("start")).toInt(1);
    o.counterStep = j.value(QStringLiteral("step")).toInt(1);
    o.counterDigits = qBound(1, j.value(QStringLiteral("digits")).toInt(1), 9);
    return o;
}
}
