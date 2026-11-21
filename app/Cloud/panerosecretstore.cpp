/*
    SPDX-FileCopyrightText: 2026 Panero contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "panerosecretstore.h"

#include <QCoreApplication>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusVariant>
#include <QEventLoop>
#include <QMap>
#include <QTimer>

namespace
{
const char SERVICE[] = "org.freedesktop.secrets";
const char SERVICE_PATH[] = "/org/freedesktop/secrets";
const char IFACE_SERVICE[] = "org.freedesktop.Secret.Service";
const char IFACE_COLLECTION[] = "org.freedesktop.Secret.Collection";
const char IFACE_ITEM[] = "org.freedesktop.Secret.Item";
const char IFACE_PROMPT[] = "org.freedesktop.Secret.Prompt";
const char IFACE_SESSION[] = "org.freedesktop.Secret.Session";
const int CALL_TIMEOUT_MS = 25000;
const int PROMPT_TIMEOUT_MS = 180000; // the user may need time to type the keyring password

using StringMap = QMap<QString, QString>;

/// The (oayays) Secret struct of the Secret Service API.
struct SecretValue {
    QDBusObjectPath session;
    QByteArray parameters;
    QByteArray value;
    QString contentType;
};
} // namespace

Q_DECLARE_METATYPE(SecretValue)

static QDBusArgument &operator<<(QDBusArgument &arg, const SecretValue &s)
{
    arg.beginStructure();
    arg << s.session << s.parameters << s.value << s.contentType;
    arg.endStructure();
    return arg;
}

static const QDBusArgument &operator>>(const QDBusArgument &arg, SecretValue &s)
{
    arg.beginStructure();
    arg >> s.session >> s.parameters >> s.value >> s.contentType;
    arg.endStructure();
    return arg;
}

namespace
{
void registerTypes()
{
    static bool done = false;
    if (done)
        return;
    qDBusRegisterMetaType<SecretValue>();
    qDBusRegisterMetaType<StringMap>();
    done = true;
}

QDBusConnection bus()
{
    return QDBusConnection::sessionBus();
}

StringMap attributesFor(const QString &key)
{
    StringMap attrs;
    attrs.insert(QStringLiteral("application"), QStringLiteral("panero"));
    attrs.insert(QStringLiteral("panero-secret"), key);
    return attrs;
}

bool call(const QString &path, const char *iface, const QString &method, const QVariantList &args, QDBusMessage *reply,
          QString *error)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(QLatin1String(SERVICE), path, QLatin1String(iface), method);
    msg.setArguments(args);
    *reply = bus().call(msg, QDBus::Block, CALL_TIMEOUT_MS);
    if (reply->type() == QDBusMessage::ErrorMessage) {
        if (error)
            *error = reply->errorMessage().isEmpty() ? reply->errorName() : reply->errorMessage();
        return false;
    }
    return true;
}

/// Waits for org.freedesktop.Secret.Prompt.Completed after calling Prompt().
class PromptWaiter : public QObject
{
    Q_OBJECT
public:
    bool run(const QDBusObjectPath &prompt, QString *error)
    {
        if (prompt.path() == QLatin1String("/") || prompt.path().isEmpty())
            return true;
        const bool connected = bus().connect(QLatin1String(SERVICE), prompt.path(), QLatin1String(IFACE_PROMPT),
                                             QStringLiteral("Completed"), this, SLOT(completed(bool, QDBusVariant)));
        if (!connected) {
            if (error)
                *error = QStringLiteral("cannot watch the keyring prompt");
            return false;
        }
        QDBusMessage reply;
        if (!call(prompt.path(), IFACE_PROMPT, QStringLiteral("Prompt"), {QString()}, &reply, error))
            return false;
        QTimer timeout;
        timeout.setSingleShot(true);
        connect(&timeout, &QTimer::timeout, &m_loop, &QEventLoop::quit);
        timeout.start(PROMPT_TIMEOUT_MS);
        if (!m_finished)
            m_loop.exec();
        bus().disconnect(QLatin1String(SERVICE), prompt.path(), QLatin1String(IFACE_PROMPT), QStringLiteral("Completed"), this,
                         SLOT(completed(bool, QDBusVariant)));
        if (!m_finished) {
            if (error)
                *error = QStringLiteral("the keyring did not answer");
            return false;
        }
        if (m_dismissed) {
            if (error)
                *error = QStringLiteral("the keyring was not unlocked");
            return false;
        }
        return true;
    }

public Q_SLOTS:
    void completed(bool dismissed, const QDBusVariant &)
    {
        m_finished = true;
        m_dismissed = dismissed;
        m_loop.quit();
    }

private:
    QEventLoop m_loop;
    bool m_finished = false;
    bool m_dismissed = false;
};

QList<QDBusObjectPath> pathList(const QVariant &v)
{
    return qdbus_cast<QList<QDBusObjectPath>>(v);
}

bool unlock(const QList<QDBusObjectPath> &objects, QString *error)
{
    if (objects.isEmpty())
        return true;
    QDBusMessage reply;
    if (!call(QLatin1String(SERVICE_PATH), IFACE_SERVICE, QStringLiteral("Unlock"), {QVariant::fromValue(objects)}, &reply, error))
        return false;
    const QVariantList out = reply.arguments();
    if (out.size() < 2)
        return false;
    PromptWaiter waiter;
    return waiter.run(qvariant_cast<QDBusObjectPath>(out.at(1)), error);
}

/// Opens a "plain" session; the secret then travels over the private session bus.
bool openSession(QDBusObjectPath *session, QString *error)
{
    QDBusMessage reply;
    if (!call(QLatin1String(SERVICE_PATH), IFACE_SERVICE, QStringLiteral("OpenSession"),
              {QStringLiteral("plain"), QVariant::fromValue(QDBusVariant(QString()))}, &reply, error))
        return false;
    const QVariantList out = reply.arguments();
    if (out.size() < 2)
        return false;
    *session = qvariant_cast<QDBusObjectPath>(out.at(1));
    return true;
}

void closeSession(const QDBusObjectPath &session)
{
    QDBusMessage reply;
    call(session.path(), IFACE_SESSION, QStringLiteral("Close"), {}, &reply, nullptr);
}

/// All items (unlocked first) carrying our attributes.
bool search(const QString &key, QList<QDBusObjectPath> *unlocked, QList<QDBusObjectPath> *locked, QString *error)
{
    QDBusMessage reply;
    if (!call(QLatin1String(SERVICE_PATH), IFACE_SERVICE, QStringLiteral("SearchItems"), {QVariant::fromValue(attributesFor(key))},
              &reply, error))
        return false;
    const QVariantList out = reply.arguments();
    if (out.size() < 2)
        return false;
    *unlocked = pathList(out.at(0));
    *locked = pathList(out.at(1));
    return true;
}

bool defaultCollection(QDBusObjectPath *collection, QString *error)
{
    QDBusMessage reply;
    if (!call(QLatin1String(SERVICE_PATH), IFACE_SERVICE, QStringLiteral("ReadAlias"), {QStringLiteral("default")}, &reply, error))
        return false;
    QDBusObjectPath path = qvariant_cast<QDBusObjectPath>(reply.arguments().value(0));
    if (path.path() == QLatin1String("/") || path.path().isEmpty()) {
        // no default collection yet: ask the keyring to create one (it prompts for a password)
        QVariantMap props;
        props.insert(QStringLiteral("org.freedesktop.Secret.Collection.Label"), QStringLiteral("Default keyring"));
        if (!call(QLatin1String(SERVICE_PATH), IFACE_SERVICE, QStringLiteral("CreateCollection"), {props, QStringLiteral("default")},
                  &reply, error))
            return false;
        const QVariantList out = reply.arguments();
        path = qvariant_cast<QDBusObjectPath>(out.value(0));
        if (path.path() == QLatin1String("/") || path.path().isEmpty()) {
            PromptWaiter waiter;
            if (!waiter.run(qvariant_cast<QDBusObjectPath>(out.value(1)), error))
                return false;
            if (!call(QLatin1String(SERVICE_PATH), IFACE_SERVICE, QStringLiteral("ReadAlias"), {QStringLiteral("default")}, &reply, error))
                return false;
            path = qvariant_cast<QDBusObjectPath>(reply.arguments().value(0));
        }
    }
    if (path.path() == QLatin1String("/") || path.path().isEmpty()) {
        if (error)
            *error = QStringLiteral("the keyring has no default collection");
        return false;
    }
    *collection = path;
    return true;
}
} // namespace

namespace PaneroSecretStore
{
bool isAvailable()
{
    if (!bus().isConnected())
        return false;
    QDBusConnectionInterface *iface = bus().interface();
    if (!iface)
        return false;
    if (iface->isServiceRegistered(QLatin1String(SERVICE)))
        return true;
    const QStringList activatable = iface->activatableServiceNames();
    return activatable.contains(QLatin1String(SERVICE));
}

QByteArray lookup(const QString &key, QString *error, bool *notFound)
{
    registerTypes();
    if (notFound)
        *notFound = false;
    if (!isAvailable()) {
        if (error)
            *error = QStringLiteral("no keyring (Secret Service) is available");
        return QByteArray();
    }
    QList<QDBusObjectPath> unlocked, locked;
    if (!search(key, &unlocked, &locked, error))
        return QByteArray();
    if (unlocked.isEmpty() && locked.isEmpty()) {
        // some keyrings hide the items of a locked collection: unlock the
        // default collection and look again before calling the key missing
        QDBusObjectPath collection;
        QString unlockError;
        if (!defaultCollection(&collection, &unlockError) || !unlock({collection}, &unlockError)) {
            if (error)
                *error = unlockError;
            return QByteArray(); // not "not found": the keyring could not be opened
        }
        if (!search(key, &unlocked, &locked, error))
            return QByteArray();
    }
    if (unlocked.isEmpty() && locked.isEmpty()) {
        if (notFound)
            *notFound = true;
        return QByteArray();
    }
    QDBusObjectPath item;
    if (!unlocked.isEmpty()) {
        item = unlocked.first();
    } else {
        if (!unlock({locked.first()}, error))
            return QByteArray();
        item = locked.first();
    }
    QDBusObjectPath session;
    if (!openSession(&session, error))
        return QByteArray();
    QDBusMessage reply;
    const bool ok = call(item.path(), IFACE_ITEM, QStringLiteral("GetSecret"), {QVariant::fromValue(session)}, &reply, error);
    closeSession(session);
    if (!ok)
        return QByteArray();
    SecretValue secret = qdbus_cast<SecretValue>(reply.arguments().value(0));
    QByteArray value = secret.value;
    wipe(secret.value);
    return value;
}

bool store(const QString &key, const QString &label, const QByteArray &secret, QString *error)
{
    registerTypes();
    if (!isAvailable()) {
        if (error)
            *error = QStringLiteral("no keyring (Secret Service) is available");
        return false;
    }
    QDBusObjectPath collection;
    if (!defaultCollection(&collection, error))
        return false;
    if (!unlock({collection}, error))
        return false;
    QDBusObjectPath session;
    if (!openSession(&session, error))
        return false;

    QVariantMap props;
    props.insert(QStringLiteral("org.freedesktop.Secret.Item.Label"), label);
    props.insert(QStringLiteral("org.freedesktop.Secret.Item.Attributes"), QVariant::fromValue(attributesFor(key)));
    SecretValue value{session, QByteArray(), secret, QStringLiteral("text/plain")};
    QDBusMessage reply;
    bool ok = call(collection.path(), IFACE_COLLECTION, QStringLiteral("CreateItem"), {props, QVariant::fromValue(value), true}, &reply,
                   error);
    wipe(value.value);
    if (ok) {
        const QVariantList out = reply.arguments();
        const QDBusObjectPath item = qvariant_cast<QDBusObjectPath>(out.value(0));
        if (item.path() == QLatin1String("/") || item.path().isEmpty()) {
            PromptWaiter waiter;
            ok = waiter.run(qvariant_cast<QDBusObjectPath>(out.value(1)), error);
        }
    }
    closeSession(session);
    return ok;
}

bool remove(const QString &key, QString *error)
{
    registerTypes();
    if (!isAvailable())
        return true;
    QList<QDBusObjectPath> unlocked, locked;
    if (!search(key, &unlocked, &locked, error))
        return false;
    if (!locked.isEmpty() && !unlock(locked, error))
        return false;
    const QList<QDBusObjectPath> items = unlocked + locked;
    for (const QDBusObjectPath &item : items) {
        QDBusMessage reply;
        if (!call(item.path(), IFACE_ITEM, QStringLiteral("Delete"), {}, &reply, error))
            return false;
        PromptWaiter waiter;
        if (!waiter.run(qvariant_cast<QDBusObjectPath>(reply.arguments().value(0)), error))
            return false;
    }
    return true;
}

void wipe(QByteArray &data)
{
    if (data.isEmpty())
        return;
    data.detach();
    volatile char *p = data.data();
    for (int i = 0; i < data.size(); ++i)
        p[i] = 0;
    data.clear();
}

void wipe(QString &data)
{
    if (data.isEmpty())
        return;
    data.detach();
    volatile ushort *p = reinterpret_cast<volatile ushort *>(data.data());
    for (int i = 0; i < data.size(); ++i)
        p[i] = 0;
    data.clear();
}
} // namespace PaneroSecretStore

#include "panerosecretstore.moc"
