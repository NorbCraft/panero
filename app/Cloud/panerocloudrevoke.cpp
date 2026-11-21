/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "panerocloudrevoke.h"
#include "panerocloudsetup.h"
#include "panerocloudutils.h"
#include "panerorclone.h"
#include "panerosecretstore.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QTimer>
#include <QUrlQuery>

namespace
{
// rclone's (public) Box client, see backend/box/box.go of rclone v1.76.0
const char BOX_CLIENT_ID[] = "d0374ba6pgmaguie02ge15sv1mllndho";
const char BOX_CLIENT_SECRET_OBSCURED[] = "sYbJYm99WB8jzeaLPU0OPDMJKIkZvD2qOn3SyEMfiJr03RdtDt3xcZEIudRhbIDL";
const int NETWORK_TIMEOUT_MS = 20000;

QString tr(const char *text)
{
    return QCoreApplication::translate("PaneroCloudRevoke", text);
}

QByteArray formEncode(const QList<QPair<QString, QString>> &fields)
{
    QByteArray out;
    for (const auto &f : fields) {
        if (!out.isEmpty())
            out += '&';
        out += QUrl::toPercentEncoding(f.first) + '=' + QUrl::toPercentEncoding(f.second);
    }
    return out;
}

/// rclone stores OAuth tokens as a JSON string in the "token" option.
QJsonObject tokenOf(const QJsonObject &remoteConfig)
{
    const QJsonValue v = remoteConfig.value(QStringLiteral("token"));
    if (v.isObject())
        return v.toObject();
    return QJsonDocument::fromJson(v.toString().toUtf8()).object();
}

QString revealBoxSecret()
{
    QProcess p;
    p.setProcessEnvironment(PaneroRclone::environment());
    p.start(PaneroRclone::program(), {QStringLiteral("reveal"), QLatin1String(BOX_CLIENT_SECRET_OBSCURED)});
    if (!p.waitForFinished(10000) || p.exitCode() != 0) {
        p.kill();
        return QString();
    }
    return QString::fromUtf8(p.readAllStandardOutput()).trimmed();
}
} // namespace

namespace PaneroCloudRevoke
{
QUrl manageUrl(const QString &type, const QJsonObject &remoteConfig)
{
    if (type == QLatin1String("drive"))
        return QUrl(QStringLiteral("https://myaccount.google.com/connections"));
    if (type == QLatin1String("onedrive")) {
        const QString driveType = remoteConfig.value(QStringLiteral("drive_type")).toString();
        if (driveType == QLatin1String("business") || driveType == QLatin1String("documentLibrary"))
            return QUrl(QStringLiteral("https://myapps.microsoft.com"));
        return QUrl(QStringLiteral("https://account.live.com/consent/Manage"));
    }
    if (type == QLatin1String("dropbox"))
        return QUrl(QStringLiteral("https://www.dropbox.com/account/connected_apps"));
    if (type == QLatin1String("pcloud"))
        return QUrl(QStringLiteral("https://my.pcloud.com/#page=settings&settings=tab-apps"));
    if (type == QLatin1String("box"))
        return QUrl(QStringLiteral("https://app.box.com/account/security"));
    if (type == QLatin1String("webdav")) {
        const QString vendor = remoteConfig.value(QStringLiteral("vendor")).toString();
        QUrl url(remoteConfig.value(QStringLiteral("url")).toString());
        if (vendor == QLatin1String("nextcloud") && url.isValid() && url.scheme().startsWith(QLatin1String("http"))) {
            QString path = url.path();
            const int dav = path.indexOf(QLatin1String("/remote.php/"));
            path = dav >= 0 ? path.left(dav) : QString();
            url.setPath(path + QLatin1String("/index.php/settings/user/security"));
            url.setQuery(QString());
            url.setUserInfo(QString());
            return url;
        }
    }
    return QUrl();
}

bool buildRequest(const QString &type, const QJsonObject &remoteConfig, const QString &boxClientSecret, Request *request, QString *why)
{
    const QJsonObject token = tokenOf(remoteConfig);
    const QString access = token.value(QStringLiteral("access_token")).toString();
    const QString refresh = token.value(QStringLiteral("refresh_token")).toString();
    Request r;
    if (type == QLatin1String("drive")) {
        const QString t = refresh.isEmpty() ? access : refresh;
        if (t.isEmpty()) {
            if (why)
                *why = tr("no token stored");
            return false;
        }
        r.url = QUrl(QStringLiteral("https://oauth2.googleapis.com/revoke"));
        r.method = "POST";
        r.headers << qMakePair(QByteArrayLiteral("Content-Type"), QByteArrayLiteral("application/x-www-form-urlencoded"));
        r.body = formEncode({{QStringLiteral("token"), t}});
    } else if (type == QLatin1String("dropbox")) {
        if (access.isEmpty()) {
            if (why)
                *why = tr("no token stored");
            return false;
        }
        r.url = QUrl(QStringLiteral("https://api.dropboxapi.com/2/auth/token/revoke"));
        r.method = "POST";
        r.headers << qMakePair(QByteArrayLiteral("Authorization"), QByteArray("Bearer " + access.toUtf8()));
        // Dropbox RPC endpoints without arguments take a JSON null
        r.headers << qMakePair(QByteArrayLiteral("Content-Type"), QByteArrayLiteral("application/json"));
        r.body = "null";
    } else if (type == QLatin1String("box")) {
        const QString t = refresh.isEmpty() ? access : refresh;
        QString clientId = remoteConfig.value(QStringLiteral("client_id")).toString();
        QString clientSecret = remoteConfig.value(QStringLiteral("client_secret")).toString();
        if (clientId.isEmpty()) {
            clientId = QLatin1String(BOX_CLIENT_ID);
            clientSecret = boxClientSecret;
        }
        if (t.isEmpty() || clientSecret.isEmpty()) {
            if (why)
                *why = t.isEmpty() ? tr("no token stored") : tr("the Box client secret is not available");
            return false;
        }
        r.url = QUrl(QStringLiteral("https://api.box.com/oauth2/revoke"));
        r.method = "POST";
        r.headers << qMakePair(QByteArrayLiteral("Content-Type"), QByteArrayLiteral("application/x-www-form-urlencoded"));
        r.body = formEncode({{QStringLiteral("client_id"), clientId}, {QStringLiteral("client_secret"), clientSecret}, {QStringLiteral("token"), t}});
    } else if (type == QLatin1String("pcloud")) {
        QString host = remoteConfig.value(QStringLiteral("hostname")).toString();
        if (host.isEmpty())
            host = QStringLiteral("api.pcloud.com");
        if (!PaneroCloudUtils::isAllowedPcloudHost(host)) {
            if (why)
                *why = tr("the stored pCloud server is not a pCloud server");
            return false;
        }
        if (access.isEmpty()) {
            if (why)
                *why = tr("no token stored");
            return false;
        }
        r.url = QUrl(QStringLiteral("https://") + host + QStringLiteral("/logout"));
        r.method = "GET";
        r.headers << qMakePair(QByteArrayLiteral("Authorization"), QByteArray("Bearer " + access.toUtf8()));
    } else {
        if (why)
            *why = type == QLatin1String("onedrive") ? tr("Microsoft has no way for apps to revoke their own access")
                                                    : tr("this service has no revocation API");
        return false;
    }
    *request = r;
    return true;
}

bool responseConfirmsRevocation(const QString &type, int httpStatus, const QByteArray &body)
{
    if (httpStatus != 200)
        return false;
    if (type == QLatin1String("pcloud")) {
        const QJsonObject o = QJsonDocument::fromJson(body).object();
        return o.value(QStringLiteral("result")).toInt(-1) == 0 && o.value(QStringLiteral("auth_deleted")).toBool(false);
    }
    return true;
}

/// Sends @p request (https only, no redirects); returns the HTTP status (0 on failure).
static int send(Request &request, QByteArray *body, QString *networkError)
{
    if (request.url.scheme() != QLatin1String("https"))
        return 0;
    QNetworkAccessManager nam;
    nam.setRedirectPolicy(QNetworkRequest::ManualRedirectPolicy); // a token never follows a redirect
    QNetworkRequest req(request.url);
    for (const auto &h : qAsConst(request.headers))
        req.setRawHeader(h.first, h.second);
    req.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    QNetworkReply *reply = request.method == "GET" ? nam.get(req) : nam.post(req, request.body);
    request.headers.clear();
    PaneroSecretStore::wipe(request.body);
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    timeout.start(NETWORK_TIMEOUT_MS);
    loop.exec();
    int status = 0;
    if (!reply->isFinished()) {
        reply->abort();
        if (networkError)
            *networkError = tr("the provider did not answer in time");
    } else {
        status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (body)
            *body = reply->readAll();
        if (status == 0 && networkError)
            *networkError = reply->errorString();
    }
    reply->deleteLater();
    return status;
}

static QJsonObject remoteConfigFromDump(const QString &remote)
{
    QByteArray dump;
    QJsonObject remoteConfig;
    if (PaneroRclone::run({QStringLiteral("config"), QStringLiteral("dump")}, &dump, nullptr, 20000) == 0)
        remoteConfig = QJsonDocument::fromJson(dump).object().value(remote).toObject();
    PaneroSecretStore::wipe(dump);
    return remoteConfig;
}

bool buildIdentityRequest(const QString &type, const QJsonObject &remoteConfig, Request *request)
{
    const QString access = tokenOf(remoteConfig).value(QStringLiteral("access_token")).toString();
    if (access.isEmpty())
        return false;
    Request r;
    r.method = "GET";
    r.headers << qMakePair(QByteArrayLiteral("Authorization"), QByteArray("Bearer " + access.toUtf8()));
    if (type == QLatin1String("drive")) {
        r.url = QUrl(QStringLiteral("https://www.googleapis.com/drive/v3/about?fields=user(displayName,emailAddress)"));
    } else if (type == QLatin1String("onedrive")) {
        r.url = QUrl(QStringLiteral("https://graph.microsoft.com/v1.0/me/drive?$select=owner,driveType"));
    } else if (type == QLatin1String("dropbox")) {
        r.url = QUrl(QStringLiteral("https://api.dropboxapi.com/2/users/get_current_account"));
        r.method = "POST";
        r.headers << qMakePair(QByteArrayLiteral("Content-Type"), QByteArrayLiteral("application/json"));
        r.body = "null";
    } else if (type == QLatin1String("box")) {
        r.url = QUrl(QStringLiteral("https://api.box.com/2.0/users/me?fields=name,login"));
    } else if (type == QLatin1String("pcloud")) {
        QString host = remoteConfig.value(QStringLiteral("hostname")).toString();
        if (host.isEmpty())
            host = QStringLiteral("api.pcloud.com");
        if (!PaneroCloudUtils::isAllowedPcloudHost(host))
            return false;
        r.url = QUrl(QStringLiteral("https://") + host + QStringLiteral("/userinfo"));
    } else {
        return false;
    }
    *request = r;
    return true;
}

QString parseIdentity(const QString &type, const QByteArray &body)
{
    const QJsonObject o = QJsonDocument::fromJson(body).object();
    QString name, mail;
    if (type == QLatin1String("drive")) {
        const QJsonObject u = o.value(QStringLiteral("user")).toObject();
        name = u.value(QStringLiteral("displayName")).toString();
        mail = u.value(QStringLiteral("emailAddress")).toString();
    } else if (type == QLatin1String("onedrive")) {
        const QJsonObject u = o.value(QStringLiteral("owner")).toObject().value(QStringLiteral("user")).toObject();
        name = u.value(QStringLiteral("displayName")).toString();
        mail = u.value(QStringLiteral("email")).toString();
        // personal accounts report no e-mail; the display name alone can be
        // copied by anyone, so add the account id Microsoft shows in its settings
        const QString id = u.value(QStringLiteral("id")).toString();
        if (mail.isEmpty() && !id.isEmpty())
            name = name.isEmpty() ? QStringLiteral("ID %1").arg(id) : QStringLiteral("%1 (ID %2)").arg(name, id);
    } else if (type == QLatin1String("dropbox")) {
        name = o.value(QStringLiteral("name")).toObject().value(QStringLiteral("display_name")).toString();
        mail = o.value(QStringLiteral("email")).toString();
    } else if (type == QLatin1String("box")) {
        name = o.value(QStringLiteral("name")).toString();
        mail = o.value(QStringLiteral("login")).toString();
    } else if (type == QLatin1String("pcloud")) {
        if (o.value(QStringLiteral("result")).toInt(-1) == 0)
            mail = o.value(QStringLiteral("email")).toString();
    }
    if (name.isEmpty())
        return mail;
    return mail.isEmpty() ? name : QStringLiteral("%1 <%2>").arg(name, mail);
}

QString accountIdentity(const QString &remote, const QString &type, QString *error)
{
    if (type == QLatin1String("pcloud") && !PaneroCloudSetup::checkPcloudHost(remote, error))
        return QString();
    QJsonObject remoteConfig = remoteConfigFromDump(remote);
    Request request;
    const bool built = buildIdentityRequest(type, remoteConfig, &request);
    remoteConfig = QJsonObject();
    if (!built) {
        if (error)
            *error = tr("no sign-in key stored");
        return QString();
    }
    QByteArray body;
    QString networkError;
    const int status = send(request, &body, &networkError);
    const QString identity = status == 200 ? parseIdentity(type, body) : QString();
    if (identity.isEmpty() && error)
        *error = status == 0 ? networkError : tr("the provider answered with status %1").arg(status);
    return identity;
}

Result revoke(const QString &remote, const QString &type)
{
    Result result;
    const bool oauth = type == QLatin1String("drive") || type == QLatin1String("dropbox") || type == QLatin1String("box")
        || type == QLatin1String("pcloud") || type == QLatin1String("onedrive");

    // a pCloud remote naming a foreign server must never get its token sent anywhere
    if (type == QLatin1String("pcloud")) {
        QString hostError;
        if (!PaneroCloudSetup::checkPcloudHost(remote, &hostError)) {
            result.outcome = Outcome::Failed;
            result.message = hostError;
            result.manageUrl = manageUrl(type, QJsonObject());
            return result;
        }
    }
    // refresh an expired access token first (Dropbox needs a valid one); errors do not matter here
    if (oauth && type != QLatin1String("onedrive"))
        PaneroRclone::run({QStringLiteral("about"), remote + QLatin1Char(':')}, nullptr, nullptr, 20000);

    QJsonObject remoteConfig = remoteConfigFromDump(remote);
    result.manageUrl = manageUrl(type, remoteConfig);

    if (!oauth) {
        result.outcome = result.manageUrl.isValid() ? Outcome::Manual : Outcome::NotNeeded;
        if (type == QLatin1String("webdav"))
            result.message = tr("If you used an app password, delete it in your server's security settings.");
        return result;
    }

    Request request;
    QString why;
    const QString boxSecret = type == QLatin1String("box") ? revealBoxSecret() : QString();
    const bool built = buildRequest(type, remoteConfig, boxSecret, &request, &why);
    remoteConfig = QJsonObject();
    if (!built) {
        result.outcome = type == QLatin1String("onedrive") ? Outcome::Manual : Outcome::Failed;
        result.message = why;
        return result;
    }

    QByteArray body;
    QString networkError;
    const int status = send(request, &body, &networkError);
    if (responseConfirmsRevocation(type, status, body)) {
        result.outcome = Outcome::Revoked;
    } else {
        result.outcome = Outcome::Failed;
        result.message = status == 0 ? networkError : tr("the provider answered with status %1").arg(status);
    }
    return result;
}
} // namespace PaneroCloudRevoke
