/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "panerocloudutils.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>

#include <KConfigCore/KConfigGroup>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

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
    // only rclone's own helper on the loopback address, with a random state
    static const QRegularExpression localRe(QStringLiteral("(http://127\\.0\\.0\\.1:53682/auth\\?state=[A-Za-z0-9_\\-]{8,})(?![^\\s])"));
    const QRegularExpressionMatch m = localRe.match(output);
    if (m.hasMatch())
        return QUrl(m.captured(1));
    return QUrl();
}

QString parseRcloneVersion(const QString &output)
{
    static const QRegularExpression re(QStringLiteral("^rclone v(\\d+\\.\\d+\\.\\d+)"), QRegularExpression::MultilineOption);
    const QRegularExpressionMatch m = re.match(output);
    return m.hasMatch() ? m.captured(1) : QString();
}

int compareVersions(const QString &a, const QString &b)
{
    const QStringList pa = a.split(QLatin1Char('.'));
    const QStringList pb = b.split(QLatin1Char('.'));
    for (int i = 0; i < qMax(pa.size(), pb.size()); ++i) {
        const int x = pa.value(i).toInt();
        const int y = pb.value(i).toInt();
        if (x != y)
            return x < y ? -1 : 1;
    }
    return 0;
}

bool isEncryptedRcloneConfig(const QByteArray &head)
{
    const QList<QByteArray> lines = head.split('\n');
    for (const QByteArray &raw : lines) {
        const QByteArray line = raw.trimmed();
        if (line.isEmpty() || line.startsWith('#'))
            continue;
        return line == "RCLONE_ENCRYPT_V0:";
    }
    return false;
}

QString safeFileName(const QString &name)
{
    if (name.isEmpty())
        return QStringLiteral("remote");
    QString f = QString::fromLatin1(QUrl::toPercentEncoding(name));
    if (f.startsWith(QLatin1Char('.')))
        f.replace(0, 1, QLatin1String("%2E"));
    return f;
}

int dirtyCacheEntries(const QString &remoteCacheDir)
{
    int dirty = 0;
    QDirIterator it(remoteCacheDir + QLatin1String("/vfsMeta"), QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        QFile f(it.next());
        if (f.size() > 1024 * 1024 || !f.open(QIODevice::ReadOnly))
            continue;
        const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
        if (o.value(QStringLiteral("Dirty")).toBool(false))
            ++dirty;
    }
    return dirty;
}

QString redactSecrets(const QString &text)
{
    QString s = text;
    // JSON fields ("access_token":"...", "pass": "...")
    static const QRegularExpression jsonRe(QStringLiteral(
        "(\"(?:access_token|refresh_token|id_token|token|client_secret|password|pass|bearer_token|code)\"\\s*:\\s*)\"(?:[^\"\\\\]|\\\\.)*\""),
        QRegularExpression::CaseInsensitiveOption);
    s.replace(jsonRe, QStringLiteral("\\1\"***\""));
    // config lines (token = {...}, pass = xyz)
    static const QRegularExpression iniRe(QStringLiteral(
        "^(\\s*(?:token|pass|password|client_secret|bearer_token|access_token|refresh_token|password2|session_token|secret_access_key)\\s*=\\s*).+$"),
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::MultilineOption);
    s.replace(iniRe, QStringLiteral("\\1***"));
    static const QRegularExpression bearerRe(QStringLiteral("(Bearer\\s+)[A-Za-z0-9._~+/=\\-]+"), QRegularExpression::CaseInsensitiveOption);
    s.replace(bearerRe, QStringLiteral("\\1***"));
    static const QRegularExpression authHeaderRe(QStringLiteral("(Authorization:\\s*)\\S+(\\s+\\S+)?"), QRegularExpression::CaseInsensitiveOption);
    s.replace(authHeaderRe, QStringLiteral("\\1***"));
    static const QRegularExpression queryRe(QStringLiteral("([?&](?:code|state|access_token|refresh_token|token|auth|password|pass)=)[^&\\s\"']+"),
                                            QRegularExpression::CaseInsensitiveOption);
    s.replace(queryRe, QStringLiteral("\\1***"));
    // rclone's "Expecting \"<state>\" got \"...\"" message
    static const QRegularExpression stateRe(QStringLiteral("(Expecting )\"[^\"]*\""));
    s.replace(stateRe, QStringLiteral("\\1\"***\""));
    return s;
}

bool isAllowedPcloudHost(const QString &host)
{
    static const QRegularExpression re(QStringLiteral("^([a-z0-9]([a-z0-9\\-]*[a-z0-9])?\\.)+pcloud\\.com$"));
    return re.match(host).hasMatch();
}

QString iniValue(const QString &text, const QString &key)
{
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString &raw : lines) {
        const QString line = raw.trimmed();
        const int eq = line.indexOf(QLatin1Char('='));
        if (eq <= 0)
            continue;
        if (line.left(eq).trimmed() == key)
            return line.mid(eq + 1).trimmed();
    }
    return QString();
}

QByteArray rcloneIniFromDump(const QJsonObject &remotes)
{
    QByteArray out;
    for (auto it = remotes.constBegin(); it != remotes.constEnd(); ++it) {
        const QString name = it.key();
        if (name.contains(QLatin1Char('[')) || name.contains(QLatin1Char(']')) || name.contains(QLatin1Char('\n')))
            continue;
        out += '[' + name.toUtf8() + "]\n";
        const QJsonObject options = it.value().toObject();
        // type first, as rclone writes it
        if (options.contains(QStringLiteral("type")))
            out += "type = " + options.value(QStringLiteral("type")).toString().toUtf8() + '\n';
        for (auto o = options.constBegin(); o != options.constEnd(); ++o) {
            if (o.key() == QLatin1String("type"))
                continue;
            QString value = o.value().isString() ? o.value().toString() : QString::fromUtf8(QJsonDocument(o.value().toObject()).toJson(QJsonDocument::Compact));
            if (o.key().contains(QLatin1Char('\n')) || value.contains(QLatin1Char('\n')) || o.key().contains(QLatin1Char('=')))
                continue;
            out += o.key().toUtf8() + " = " + value.toUtf8() + '\n';
        }
        out += '\n';
    }
    return out;
}

QString oneDriveScopes(bool readOnly, bool includeShared)
{
    QStringList scopes;
    if (readOnly) {
        scopes << QStringLiteral("Files.Read");
        if (includeShared)
            scopes << QStringLiteral("Files.Read.All");
    } else {
        scopes << QStringLiteral("Files.Read") << QStringLiteral("Files.ReadWrite");
        if (includeShared)
            scopes << QStringLiteral("Files.Read.All") << QStringLiteral("Files.ReadWrite.All");
    }
    scopes << QStringLiteral("offline_access");
    return scopes.join(QLatin1Char(' '));
}

QString vfsCacheFile(const QString &remoteCacheDir, const QString &remote, const QString &relativePath)
{
    QString rel = QDir::cleanPath(relativePath);
    while (rel.startsWith(QLatin1Char('/')))
        rel.remove(0, 1);
    if (rel == QLatin1String(".") || rel.startsWith(QLatin1String("../")) || rel == QLatin1String(".."))
        return QString();
    return remoteCacheDir + QLatin1String("/vfs/") + remote + (rel.isEmpty() ? QString() : QLatin1Char('/') + rel);
}

QString vfsMetaFile(const QString &remoteCacheDir, const QString &remote, const QString &relativePath)
{
    const QString data = vfsCacheFile(remoteCacheDir, remote, relativePath);
    if (data.isEmpty())
        return QString();
    return remoteCacheDir + QLatin1String("/vfsMeta/") + data.mid(remoteCacheDir.length() + int(qstrlen("/vfs/")));
}

bool isDirtyMeta(const QString &metaFile)
{
    QFile f(metaFile);
    if (f.size() > 1024 * 1024 || !f.open(QIODevice::ReadOnly))
        return false;
    return QJsonDocument::fromJson(f.readAll()).object().value(QStringLiteral("Dirty")).toBool(false);
}

QString aliasCacheSubPath(const QString &aliasTarget)
{
    const QString t = aliasTarget.trimmed();
    const int colon = t.indexOf(QLatin1Char(':'));
    const int slash = t.indexOf(QLatin1Char('/'));
    QString name, path;
    if (colon > 0 && (slash < 0 || colon < slash)) {
        name = t.left(colon);
        path = t.mid(colon + 1);
    } else {
        name = QStringLiteral("local");
        path = t;
    }
    path = QDir::cleanPath(path);
    while (path.startsWith(QLatin1Char('/')))
        path.remove(0, 1);
    if (path == QLatin1String("."))
        path.clear();
    return path.isEmpty() ? name : name + QLatin1Char('/') + path;
}

bool isFullyCached(const QString &cacheFile, qint64 size)
{
    if (cacheFile.isEmpty())
        return false;
    const QByteArray path = QFile::encodeName(cacheFile);
    struct stat st;
    if (::stat(path.constData(), &st) != 0 || !S_ISREG(st.st_mode))
        return false;
    if (size <= 0)
        return true;
    if (st.st_size < size)
        return false;
    const int fd = ::open(path.constData(), O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return false;
    const off_t hole = ::lseek(fd, 0, SEEK_HOLE);
    ::close(fd);
    // the end of the file counts as a hole: no earlier hole means fully downloaded
    return hole >= 0 && hole >= off_t(size);
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
        // rclone reports fatal errors at NOTICE level ("NOTICE: Fatal error: ...")
        if (skipRe.match(raw.trimmed()).hasMatch() && !raw.contains(QLatin1String("Fatal error"))
            && !raw.contains(QLatin1String("Failed to")))
            continue;
        const QString line = cleanRcloneLogLine(raw);
        if (line.startsWith(QLatin1String("Usage:")))
            break;
        if (line.isEmpty() || out.contains(line))
            continue;
        out << redactSecrets(line);
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
    // not the cache folder (PaneroRclone::cacheBase()): a mount point must be empty
    return QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + QLatin1String("/panero/mounts");
}

QString userRcloneConfigPath()
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
