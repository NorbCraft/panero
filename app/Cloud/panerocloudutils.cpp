/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "panerocloudutils.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>

#include <KConfigCore/KConfigGroup>

namespace PaneroCloudUtils
{
QList<Remote> parseListRemotes(const QString &output)
{
    QList<Remote> remotes;
    const QStringList lines = output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &raw : lines) {
        const QString line = raw.trimmed();
        // "name:   type" (--long) or just "name:" (plain listremotes)
        const int colon = line.indexOf(QLatin1Char(':'));
        if (colon <= 0)
            continue;
        Remote r;
        r.name = line.left(colon).trimmed();
        r.type = line.mid(colon + 1).trimmed();
        if (r.name.isEmpty())
            continue;
        remotes << r;
    }
    return remotes;
}

static bool isNameChar(QChar c)
{
    const ushort u = c.unicode();
    return (u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z') || (u >= '0' && u <= '9') || u == '_' || u == '-' || u == '.';
}

QString sanitizeRemoteName(const QString &label)
{
    QString name;
    // decompose accents (é -> e) so "Résumés" stays readable
    const QString decomposed = label.normalized(QString::NormalizationForm_KD);
    for (const QChar c : decomposed) {
        if (isNameChar(c))
            name += c;
    }
    while (!name.isEmpty() && (name.at(0) == QLatin1Char('-') || name.at(0) == QLatin1Char('.')))
        name.remove(0, 1);
    if (name.isEmpty())
        name = QStringLiteral("Cloud");
    return name;
}

QString uniqueRemoteName(const QString &label, const QStringList &existing)
{
    const QString base = sanitizeRemoteName(label);
    QString name = base;
    for (int i = 2; existing.contains(name, Qt::CaseSensitive); ++i)
        name = base + QString::number(i);
    return name;
}

bool isValidRemoteName(const QString &name)
{
    static const QRegularExpression re(QStringLiteral("^[A-Za-z0-9_][A-Za-z0-9_.\\-]*(?: [A-Za-z0-9_.\\-]+)*$"));
    return re.match(name).hasMatch();
}

QUrl buildServerUrl(const QString &scheme, const QString &host, int port, const QString &folder, const QString &user)
{
    QString h = host.trimmed();
    if (h.isEmpty())
        return QUrl();

    QUrl url;
    if (h.contains(QLatin1String("://"))) {
        // pasted a full address: keep host, port, path and user from it
        const QUrl pasted(h, QUrl::TolerantMode);
        if (!pasted.isValid() || pasted.host().isEmpty())
            return QUrl();
        url.setHost(pasted.host());
        if (pasted.port() > 0)
            url.setPort(pasted.port());
        url.setPath(pasted.path());
        url.setUserName(pasted.userName());
    } else {
        // "host/folder" or "host:port"
        QString path;
        const int slash = h.indexOf(QLatin1Char('/'));
        if (slash >= 0) {
            path = h.mid(slash);
            h = h.left(slash);
        }
        const int at = h.lastIndexOf(QLatin1Char('@'));
        if (at > 0) {
            url.setUserName(h.left(at));
            h = h.mid(at + 1);
        }
        // host:port, but not an IPv6 literal without brackets
        const int colon = h.lastIndexOf(QLatin1Char(':'));
        if (colon > 0 && h.count(QLatin1Char(':')) == 1) {
            bool ok = false;
            const int p = h.mid(colon + 1).toInt(&ok);
            if (ok && p > 0)
                url.setPort(p);
            h = h.left(colon);
        } else if (h.startsWith(QLatin1Char('[')) && h.contains(QLatin1String("]:"))) {
            const int close = h.indexOf(QLatin1String("]:"));
            bool ok = false;
            const int p = h.mid(close + 2).toInt(&ok);
            if (ok && p > 0)
                url.setPort(p);
            h = h.left(close + 1);
        }
        if (h.startsWith(QLatin1Char('[')) && h.endsWith(QLatin1Char(']')))
            h = h.mid(1, h.length() - 2);
        url.setHost(h);
        url.setPath(path);
    }
    url.setScheme(scheme);
    if (url.host().isEmpty())
        return QUrl();

    if (port > 0)
        url.setPort(port);
    const QString f = folder.trimmed();
    if (!f.isEmpty())
        url.setPath(f.startsWith(QLatin1Char('/')) ? f : QLatin1Char('/') + f);
    if (url.path().isEmpty())
        url.setPath(QStringLiteral("/"));
    if (!user.trimmed().isEmpty())
        url.setUserName(user.trimmed());
    url.setPassword(QString());
    return url.isValid() ? url : QUrl();
}

QString defaultServerLabel(const QUrl &url)
{
    QString label = url.host();
    if (!url.userName().isEmpty())
        label = url.userName() + QLatin1Char('@') + label;
    const QString folder = QFileInfo(url.path()).fileName();
    if (!folder.isEmpty())
        label += QLatin1String(" (") + folder + QLatin1Char(')');
    return label;
}

QList<Server> readServers(const KConfigGroup &group)
{
    QList<Server> servers;
    const QStringList urls = group.readEntry("Servers", QStringList());
    const QStringList labels = group.readEntry("Labels", QStringList());
    for (int i = 0; i < urls.count(); ++i) {
        Server s;
        s.url = QUrl(urls.at(i));
        if (!s.url.isValid() || s.url.scheme().isEmpty())
            continue;
        s.url.setPassword(QString());
        s.label = labels.value(i);
        if (s.label.isEmpty())
            s.label = defaultServerLabel(s.url);
        servers << s;
    }
    return servers;
}

void writeServers(KConfigGroup &group, const QList<Server> &servers)
{
    QStringList urls, labels;
    for (const Server &s : servers) {
        QUrl u = s.url;
        u.setPassword(QString()); // never store passwords; KIO/KWallet keep them
        urls << u.toString();
        labels << s.label;
    }
    group.writeEntry("Servers", urls);
    group.writeEntry("Labels", labels);
    group.sync();
}

QString remoteLabel(const KConfigGroup &group, const QString &remote)
{
    const QStringList names = group.readEntry("Remote Names", QStringList());
    const QStringList labels = group.readEntry("Remote Labels", QStringList());
    const int i = names.indexOf(remote);
    const QString label = i >= 0 ? labels.value(i) : QString();
    return label.isEmpty() ? remote : label;
}

void setRemoteLabel(KConfigGroup &group, const QString &remote, const QString &label)
{
    QStringList names = group.readEntry("Remote Names", QStringList());
    QStringList labels = group.readEntry("Remote Labels", QStringList());
    while (labels.count() < names.count())
        labels << QString();
    const int i = names.indexOf(remote);
    if (label.isEmpty() || label == remote) {
        if (i >= 0) {
            names.removeAt(i);
            labels.removeAt(i);
        }
    } else if (i >= 0) {
        labels[i] = label;
    } else {
        names << remote;
        labels << label;
    }
    group.writeEntry("Remote Names", names);
    group.writeEntry("Remote Labels", labels);
    group.sync();
}

static QString unescapeMountInfo(const QByteArray &field)
{
    QByteArray out;
    out.reserve(field.size());
    for (int i = 0; i < field.size(); ++i) {
        if (field.at(i) == '\\' && i + 3 < field.size()) {
            const QByteArray oct = field.mid(i + 1, 3);
            bool ok = false;
            const int v = oct.toInt(&ok, 8);
            if (ok && oct.size() == 3) {
                out += char(v);
                i += 3;
                continue;
            }
        }
        out += field.at(i);
    }
    return QString::fromLocal8Bit(out);
}

QStringList parseMountInfo(const QByteArray &mountinfo)
{
    // "36 35 98:0 /mnt1 /mnt/parent rw,noatime master:1 - ext3 /dev/root rw"
    QStringList points;
    const QList<QByteArray> lines = mountinfo.split('\n');
    for (const QByteArray &line : lines) {
        const QList<QByteArray> fields = line.split(' ');
        if (fields.size() < 5)
            continue;
        points << unescapeMountInfo(fields.at(4));
    }
    return points;
}

QUrl parseAuthUrl(const QString &output)
{
    static const QRegularExpression linkRe(QStringLiteral("following link:\\s*(https?://\\S+)"));
    QRegularExpressionMatch m = linkRe.match(output);
    if (m.hasMatch())
        return QUrl(m.captured(1));
    static const QRegularExpression localRe(QStringLiteral("(http://(?:127\\.0\\.0\\.1|localhost):\\d+/auth\\S*)"));
    m = localRe.match(output);
    if (m.hasMatch())
        return QUrl(m.captured(1));
    return QUrl();
}

QString cleanRcloneLogLine(const QString &line)
{
    // "<3>ERROR : x" (stderr not a tty, systemd style) or "2026/11/20 19:57:32 ERROR : x"
    static const QRegularExpression prefixRe(QStringLiteral("^(?:<\\d>)?(?:\\d{4}/\\d\\d/\\d\\d \\d\\d:\\d\\d:\\d\\d )?"));
    static const QRegularExpression tagRe(QStringLiteral("^(?:ERROR|NOTICE|INFO|DEBUG|CRITICAL)\\s*:\\s*(?::\\s*)?"));
    QString s = line.trimmed();
    s.remove(prefixRe);
    s.remove(tagRe);
    return s.trimmed();
}

QString rcloneErrorSummary(const QString &stderrText, int maxLines)
{
    static const QRegularExpression skipRe(QStringLiteral("^(?:<[5-7]>)?(?:\\d{4}/\\d\\d/\\d\\d \\d\\d:\\d\\d:\\d\\d )?(?:NOTICE|INFO|DEBUG)\\b"));
    QStringList out;
    const QStringList lines = stderrText.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &raw : lines) {
        if (skipRe.match(raw.trimmed()).hasMatch())
            continue;
        const QString line = cleanRcloneLogLine(raw);
        if (line.startsWith(QLatin1String("Usage:")))
            break;
        if (line.isEmpty() || out.contains(line))
            continue;
        out << line;
    }
    while (out.count() > maxLines)
        out.removeFirst();
    return out.join(QLatin1Char('\n'));
}

QString mountBaseDir()
{
    if (!qEnvironmentVariableIsEmpty("XDG_RUNTIME_DIR")) {
        const QString runtime = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
        if (!runtime.isEmpty())
            return runtime + QLatin1String("/panero-cloud");
    }
    return QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + QLatin1String("/panero/cloud");
}

QString rcloneConfigPath()
{
    const QString env = qEnvironmentVariable("RCLONE_CONFIG");
    if (!env.isEmpty())
        return env;
    const QString legacy = QDir::homePath() + QLatin1String("/.rclone.conf");
    if (QFileInfo::exists(legacy))
        return legacy;
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QLatin1String("/rclone/rclone.conf");
}
} // namespace PaneroCloudUtils
