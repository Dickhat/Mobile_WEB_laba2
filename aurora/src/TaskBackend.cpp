#include "TaskBackend.h"

#include <QCryptographicHash>
#include <QDate>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QNetworkRequest>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QStandardPaths>
#include <QSslCertificate>
#include <QSslError>
#include <QUuid>
#include <QUrl>
#include <QVariantMap>

TaskBackend::TaskBackend(QObject *parent)
    : QObject(parent), settings_(QStringLiteral("ru.mai"), QStringLiteral("lab2tasks"))
{
    serverUrl_ = settings_.value("url", "https://192.168.1.100:8443").toString();
    fingerprint_ = settings_.value("fingerprint").toString();
    username_ = settings_.value("username").toString();
    token_ = settings_.value("token").toString(); // lab-only; plain QSettings, not a secure vault
    initDb();
    connect(&periodicSync_, &QTimer::timeout, this, [this]() {
        if (!offline_ && !busy_ && authenticated()) sync();
    });
    periodicSync_.start(25000);
}

TaskBackend::~TaskBackend() {
    // Qt owns the database connection until process exit.
    if (db_.isValid()) db_.close();
}

bool TaskBackend::initDb() {
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!QDir().mkpath(base)) { setMessage("Не удалось создать каталог приложения"); return false; }
    db_ = QSqlDatabase::addDatabase("QSQLITE", "taskCache");
    db_.setDatabaseName(base + "/tasks.sqlite3");
    if (!db_.open()) { setMessage("SQLite: " + db_.lastError().text()); return false; }
    QSqlQuery q(db_);
    const bool ok = q.exec(
        "CREATE TABLE IF NOT EXISTS tasks ("
        "local_id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "origin TEXT NOT NULL, account TEXT NOT NULL,"
        "remote_id INTEGER, client_uid TEXT NOT NULL,"
        "title TEXT NOT NULL, description TEXT NOT NULL, status TEXT NOT NULL,"
        "due_date TEXT, dirty TEXT NOT NULL DEFAULT 'none', deleted INTEGER NOT NULL DEFAULT 0,"
        "UNIQUE(origin,account,client_uid), UNIQUE(origin,account,remote_id))");
    if (!ok) setMessage("SQLite schema: " + q.lastError().text());
    return ok;
}

void TaskBackend::setMessage(const QString &message) { message_ = message; emit messageChanged(); }
void TaskBackend::setBusy(bool b) { if (busy_ == b) return; busy_ = b; emit busyChanged(); }
void TaskBackend::notifyTasks() { emit tasksChanged(); }
QString TaskBackend::normalizeDueDate(const QString &date) const { return date.trimmed(); }
QString TaskBackend::syncKey() const {
    return QStringLiteral("etag/") + QString::fromLatin1(QCryptographicHash::hash(
        (serverUrl_ + "|" + username_).toUtf8(), QCryptographicHash::Sha256).toHex());
}

QVariantList TaskBackend::tasks() const {
    QVariantList list;
    if (!db_.isOpen() || username_.isEmpty()) return list;
    QSqlQuery q(db_);
    q.prepare("SELECT local_id,remote_id,title,description,status,COALESCE(due_date,'') AS due_date,dirty "
              "FROM tasks WHERE origin=? AND account=? AND deleted=0 ORDER BY local_id DESC");
    q.addBindValue(serverUrl_); q.addBindValue(username_);
    if (!q.exec()) return list;
    while (q.next()) {
        QVariantMap t;
        t.insert("localId", q.value(0));
        t.insert("remoteId", q.value(1));
        t.insert("title", q.value(2));
        t.insert("description", q.value(3));
        t.insert("status", q.value(4));
        t.insert("dueDate", q.value(5));
        t.insert("dirty", q.value(6));
        list.append(t);
    }
    return list;
}

void TaskBackend::configure(const QString &url, const QString &sha256Hex) {
    if (busy_) { setMessage("Дождитесь завершения операции"); return; }
    const QString clean = url.trimmed().endsWith('/') ? url.trimmed().left(url.trimmed().size()-1) : url.trimmed();
    const QUrl parsed(clean);
    const QString fp = QString(sha256Hex).remove(':').remove(' ').trimmed().toUpper();
    if (parsed.scheme() != "https" || parsed.host().isEmpty() || fp.size() != 64) {
        setMessage("Нужны URL https://IP:8443 и SHA-256 сертификата (64 символа)"); return;
    }
    for (const QChar &ch : fp) {
        if (!ch.isDigit() && !(ch >= QChar('A') && ch <= QChar('F'))) {
            setMessage("Отпечаток SHA-256 должен содержать только 0–9, A–F"); return;
        }
    }
    if (serverUrl_ != clean || fingerprint_ != fp) {
        // A new HTTPS endpoint/certificate must not silently reuse a JWT.
        token_.clear(); settings_.remove("token"); emit authChanged();
    }
    serverUrl_ = clean; fingerprint_ = fp;
    settings_.setValue("url", serverUrl_); settings_.setValue("fingerprint", fingerprint_);
    emit configChanged(); notifyTasks(); setMessage("Адрес и отпечаток сохранены. Авторизуйтесь.");
}

void TaskBackend::setOffline(bool value) {
    if (offline_ == value) return;
    offline_ = value; emit offlineChanged();
    setMessage(value ? "OFFLINE: используется локальная база SQLite" : "ONLINE: подключение разрешено");
    if (!value && !busy_ && authenticated()) sync();
}

void TaskBackend::request(const QByteArray &method, const QString &path,
                          const QJsonObject &body, const Callback &done,
                          const QByteArray &ifNoneMatch) {
    QNetworkRequest req(QUrl(serverUrl_ + path));
    req.setRawHeader("Accept", "application/json");
    req.setRawHeader("User-Agent", "AuroraTasksLab2/1.0 QtNetwork");
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json; charset=utf-8");
    // Host is populated automatically by QNetworkAccessManager.
    if (!token_.isEmpty()) req.setRawHeader("Authorization", QByteArray("Bearer ") + token_.toUtf8());
    if (!ifNoneMatch.isEmpty()) req.setRawHeader("If-None-Match", ifNoneMatch);
    QNetworkReply *reply = nullptr;
    if (method == "GET") reply = network_.get(req);
    else if (method == "POST") reply = network_.post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    else if (method == "PUT") reply = network_.sendCustomRequest(req,"PUT", QJsonDocument(body).toJson(QJsonDocument::Compact));
    else if (method == "DELETE") reply = network_.deleteResource(req);
    if (!reply) { done(0, QByteArray(), "Неподдерживаемый HTTP-метод", QByteArray()); return; }

    connect(reply, &QNetworkReply::sslErrors, reply, [this, reply](const QList<QSslError> &errors) {
        // Trust one explicitly pinned self-signed certificate, never all certificates.
        const QSslCertificate peer = reply->sslConfiguration().peerCertificate();
        const QString actual = QString::fromLatin1(peer.digest(QCryptographicHash::Sha256).toHex()).toUpper();
        bool onlySelfSigned = !errors.isEmpty();
        for (const QSslError &e : errors) {
            if (e.error() != QSslError::SelfSignedCertificate &&
                e.error() != QSslError::CertificateUntrusted &&
                e.error() != QSslError::UnableToGetLocalIssuerCertificate)
                onlySelfSigned = false;
        }
        if (onlySelfSigned && !fingerprint_.isEmpty() && actual == fingerprint_) reply->ignoreSslErrors();
        // Hostname mismatch and expired certificate are NEVER ignored.
    });
    QTimer *timeout = new QTimer(reply);
    timeout->setSingleShot(true); timeout->start(10000);
    connect(timeout, &QTimer::timeout, reply, [reply]() {
        reply->setProperty("auroraTimedOut", true);
        reply->abort();
    });
    connect(reply, &QNetworkReply::finished, reply, [reply, done, timeout]() {
        timeout->stop();
        const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray bytes = reply->readAll();
        const QString error = reply->property("auroraTimedOut").toBool()
                                  ? QStringLiteral("Таймаут HTTPS: 10 секунд")
                                  : ((reply->error() == QNetworkReply::NoError || code != 0)
                                         ? QString() : reply->errorString());
        const QByteArray etag = reply->rawHeader("ETag");
        done(code, bytes, error, etag);
        reply->deleteLater();
    });
}

void TaskBackend::login(const QString &name, const QString &password) {
    if (busy_ || offline_) { setMessage("Для входа требуется сеть и отсутствие активных запросов"); return; }
    if (fingerprint_.isEmpty()) { setMessage("Сначала укажите отпечаток HTTPS-сертификата"); return; }
    if (name.trimmed().isEmpty() || password.isEmpty()) { setMessage("Введите логин и пароль"); return; }
    setBusy(true);
    QJsonObject body; body["username"] = name.trimmed(); body["password"] = password;
    request("POST", "/api/v1/auth/login", body, [this, name](int code, const QByteArray &bytes, const QString &error, const QByteArray &) {
        setBusy(false);
        if (!error.isEmpty() || code != 200) {
            setMessage(error.isEmpty() ? QString("Ошибка входа HTTP %1: %2").arg(code).arg(QString::fromUtf8(bytes)) : error);
            return;
        }
        QJsonParseError pe;
        const QJsonDocument data = QJsonDocument::fromJson(bytes, &pe);
        const QString newToken = data.object().value("access_token").toString();
        if (pe.error != QJsonParseError::NoError || newToken.isEmpty()) {
            setMessage("Сервер прислал некорректный JSON токена"); return;
        }
        token_ = newToken; username_ = name.trimmed();
        settings_.setValue("token", token_); settings_.setValue("username", username_);
        emit authChanged(); notifyTasks(); setMessage("Авторизация успешна. Синхронизация..."); sync();
    });
}

void TaskBackend::logout() {
    if (busy_) { setMessage("Дождитесь окончания синхронизации"); return; }
    token_.clear(); settings_.remove("token"); emit authChanged();
    setMessage("Выход. Кэш текущего пользователя не удалён.");
}

bool TaskBackend::canEdit() const { return !username_.isEmpty() && !busy_ && db_.isOpen(); }

void TaskBackend::createTask(const QString &title, const QString &description,
                             const QString &status, const QString &dueDate) {
    if (!canEdit()) { setMessage("Дождитесь синхронизации или сначала войдите"); return; }
    if (title.trimmed().isEmpty() || title.size() > 120 || description.size() > 1000) {
        setMessage("Заголовок: 1–120 символов, описание: до 1000"); return;
    }
    if (status != "new" && status != "in_progress" && status != "done") {
        setMessage("Некорректный статус"); return;
    }
    const QString due = normalizeDueDate(dueDate);
    if (!due.isEmpty() && !QDate::fromString(due, Qt::ISODate).isValid()) {
        setMessage("Срок должен быть YYYY-MM-DD"); return;
    }
    QSqlQuery q(db_);
    q.prepare("INSERT INTO tasks(origin,account,client_uid,title,description,status,due_date,dirty,deleted) "
              "VALUES(?,?,?,?,?,?,?,'create',0)");
    q.addBindValue(serverUrl_); q.addBindValue(username_);
    q.addBindValue(QUuid::createUuid().toString(QUuid::WithoutBraces));
    q.addBindValue(title.trimmed()); q.addBindValue(description); q.addBindValue(status);
    q.addBindValue(due.isEmpty() ? QVariant(QVariant::String) : QVariant(due));
    if (!q.exec()) { setMessage("Ошибка SQLite: " + q.lastError().text()); return; }
    notifyTasks(); setMessage("Задача сохранена локально (ожидает синхронизации)");
    if (!offline_ && authenticated()) sync();
}

void TaskBackend::updateTask(int localId, const QString &title, const QString &description,
                             const QString &status, const QString &dueDate) {
    if (!canEdit()) { setMessage("Дождитесь синхронизации"); return; }
    if (title.trimmed().isEmpty() || title.size() > 120 || description.size() > 1000 ||
        (status != "new" && status != "in_progress" && status != "done")) {
        setMessage("Неверные поля задачи"); return;
    }
    const QString due = normalizeDueDate(dueDate);
    if (!due.isEmpty() && !QDate::fromString(due, Qt::ISODate).isValid()) {
        setMessage("Срок должен быть YYYY-MM-DD"); return;
    }
    QSqlQuery q(db_);
    q.prepare("UPDATE tasks SET title=?,description=?,status=?,due_date=?,"
              "dirty=CASE WHEN dirty='create' THEN 'create' ELSE 'update' END "
              "WHERE local_id=? AND origin=? AND account=? AND deleted=0");
    q.addBindValue(title.trimmed()); q.addBindValue(description); q.addBindValue(status);
    q.addBindValue(due.isEmpty() ? QVariant(QVariant::String) : QVariant(due));
    q.addBindValue(localId); q.addBindValue(serverUrl_); q.addBindValue(username_);
    if (!q.exec()) { setMessage("SQLite: " + q.lastError().text()); return; }
    notifyTasks(); setMessage("Изменение сохранено локально");
    if (!offline_ && authenticated()) sync();
}

void TaskBackend::deleteTask(int localId) {
    if (!canEdit()) { setMessage("Дождитесь синхронизации"); return; }
    QSqlQuery q(db_);
    // A task created and deleted offline must never reach the server.
    q.prepare("DELETE FROM tasks WHERE local_id=? AND origin=? AND account=? AND dirty='create'");
    q.addBindValue(localId); q.addBindValue(serverUrl_); q.addBindValue(username_);
    if (!q.exec()) { setMessage("SQLite: " + q.lastError().text()); return; }
    if (q.numRowsAffected() == 0) {
        q.prepare("UPDATE tasks SET dirty='delete',deleted=1 WHERE local_id=? AND origin=? AND account=?");
        q.addBindValue(localId); q.addBindValue(serverUrl_); q.addBindValue(username_);
        if (!q.exec()) { setMessage("SQLite: " + q.lastError().text()); return; }
    }
    notifyTasks(); setMessage("Удаление отмечено в SQLite");
    if (!offline_ && authenticated()) sync();
}

void TaskBackend::sync() {
    if (offline_ || busy_ || !authenticated()) {
        if (offline_) setMessage("OFFLINE: показаны данные SQLite, очередь сохранена");
        return;
    }
    if (fingerprint_.isEmpty()) { setMessage("Укажите сертификат в настройках"); return; }
    setBusy(true); setMessage("Синхронизация: отправка локальной очереди...");
    syncNext();
}

void TaskBackend::syncNext() {
    QSqlQuery q(db_);
    q.prepare("SELECT local_id,remote_id,client_uid,title,description,status,due_date,dirty "
              "FROM tasks WHERE origin=? AND account=? AND dirty<>'none' ORDER BY local_id LIMIT 1");
    q.addBindValue(serverUrl_); q.addBindValue(username_);
    if (!q.exec()) { setBusy(false); setMessage("SQLite: " + q.lastError().text()); return; }
    if (!q.next()) { downloadTasks(); return; }

    const int localId = q.value(0).toInt();
    const int remoteId = q.value(1).toInt();
    const QString operation = q.value(7).toString();
    QJsonObject body;
    body["title"] = q.value(3).toString();
    body["description"] = q.value(4).toString();
    body["status"] = q.value(5).toString();
    body["due_date"] = q.value(6).isNull() ? QJsonValue(QJsonValue::Null) : QJsonValue(q.value(6).toString());
    QByteArray method; QString path;
    if (operation == "create") {
        method = "POST"; path = "/api/v1/tasks";
        body["client_uid"] = q.value(2).toString();
    } else if (operation == "update" && remoteId > 0) {
        method = "PUT"; path = "/api/v1/tasks/" + QString::number(remoteId);
    } else if (operation == "delete" && remoteId > 0) {
        method = "DELETE"; path = "/api/v1/tasks/" + QString::number(remoteId);
    } else { setBusy(false); setMessage("Очередь повреждена: отсутствует ID сервера"); return; }

    request(method, path, body, [this, localId, operation](int code, const QByteArray &bytes,
                                                           const QString &error, const QByteArray &) {
        if (!error.isEmpty() || code == 0) {
            setBusy(false); setMessage("Нет сети / HTTPS: " + error + ". Очередь сохранена."); return;
        }
        if (code == 401) {
            token_.clear(); settings_.remove("token"); emit authChanged();
            setBusy(false); setMessage("HTTP 401: войдите снова. Локальный кэш сохранён."); return;
        }
        if (code < 200 || code >= 300) {
            setBusy(false);
            setMessage(QString("HTTP %1: %2. Очередь сохранена.").arg(code).arg(QString::fromUtf8(bytes))); return;
        }
        QSqlQuery update(db_);
        if (operation == "delete") {
            update.prepare("DELETE FROM tasks WHERE local_id=?");
            update.addBindValue(localId);
        } else {
            QJsonParseError pe;
            const QJsonDocument doc = QJsonDocument::fromJson(bytes, &pe);
            const int remoteId = doc.object().value("id").toInt();
            if (pe.error != QJsonParseError::NoError || !doc.isObject() || remoteId <= 0) {
                setBusy(false); setMessage("Некорректный JSON ответа сервера, запись оставлена в очереди"); return;
            }
            update.prepare("UPDATE tasks SET remote_id=?, dirty='none',deleted=0 WHERE local_id=?");
            update.addBindValue(remoteId); update.addBindValue(localId);
        }
        if (!update.exec()) { setBusy(false); setMessage("SQLite: " + update.lastError().text()); return; }
        notifyTasks(); syncNext();
    });
}

void TaskBackend::downloadTasks() {
    const QByteArray oldTag = settings_.value(syncKey()).toByteArray();
    request("GET", "/api/v1/tasks", QJsonObject(), [this](int code, const QByteArray &bytes,
                                                              const QString &error, const QByteArray &etag) {
        if (!error.isEmpty() || code == 0) {
            setBusy(false); setMessage("Нет сети: " + error + ". Показаны данные SQLite."); return;
        }
        if (code == 304) { setBusy(false); setMessage("HTTP 304: кэш актуален, изменений нет."); return; }
        if (code == 401) {
            token_.clear(); settings_.remove("token"); emit authChanged();
            setBusy(false); setMessage("HTTP 401: токен истёк, войдите снова"); return;
        }
        if (code != 200) {
            setBusy(false); setMessage(QString("Ошибка HTTP %1: %2").arg(code).arg(QString::fromUtf8(bytes))); return;
        }
        QJsonParseError pe;
        const QJsonDocument doc = QJsonDocument::fromJson(bytes, &pe);
        if (pe.error != QJsonParseError::NoError || !doc.isArray()) {
            setBusy(false); setMessage("Неверный JSON списка задач"); return;
        }
        if (!db_.transaction()) { setBusy(false); setMessage("SQLite: не удалось начать транзакцию"); return; }
        bool success = true;
        QList<int> seen;
        const QJsonArray items = doc.array();
        for (const QJsonValue &value : items) {
            const QJsonObject item = value.toObject();
            const int id = item.value("id").toInt();
            if (id <= 0) { success = false; break; }
            seen << id;
            QSqlQuery find(db_);
            find.prepare("SELECT local_id,dirty FROM tasks WHERE origin=? AND account=? AND remote_id=?");
            find.addBindValue(serverUrl_); find.addBindValue(username_); find.addBindValue(id);
            if (!find.exec()) { success=false; break; }
            if (find.next() && find.value(1).toString() != "none") continue;
            QSqlQuery w(db_);
            const QJsonValue due = item.value("due_date");
            const QVariant dueParam = due.isNull() ? QVariant(QVariant::String) : QVariant(due.toString());
            if (find.isValid()) {
                w.prepare("UPDATE tasks SET client_uid=?,title=?,description=?,status=?,due_date=?,deleted=0 "
                          "WHERE origin=? AND account=? AND remote_id=?");
                w.addBindValue(item.value("client_uid").toString());
                w.addBindValue(item.value("title").toString());
                w.addBindValue(item.value("description").toString());
                w.addBindValue(item.value("status").toString()); w.addBindValue(dueParam);
                w.addBindValue(serverUrl_); w.addBindValue(username_); w.addBindValue(id);
            } else {
                w.prepare("INSERT INTO tasks(origin,account,remote_id,client_uid,title,description,status,due_date,dirty,deleted) "
                          "VALUES(?,?,?,?,?,?,?,?,'none',0)");
                w.addBindValue(serverUrl_); w.addBindValue(username_); w.addBindValue(id);
                w.addBindValue(item.value("client_uid").toString()); w.addBindValue(item.value("title").toString());
                w.addBindValue(item.value("description").toString()); w.addBindValue(item.value("status").toString());
                w.addBindValue(dueParam);
            }
            if (!w.exec()) { success=false; break; }
        }
        if (success) {
            // Remove synced rows deleted on the server. Pending offline rows are never removed.
            QSqlQuery existing(db_);
            existing.prepare("SELECT local_id,remote_id FROM tasks WHERE origin=? AND account=? AND dirty='none' AND remote_id IS NOT NULL");
            existing.addBindValue(serverUrl_); existing.addBindValue(username_);
            success = existing.exec();
            while (success && existing.next()) {
                const int remote = existing.value(1).toInt();
                if (!seen.contains(remote)) {
                    QSqlQuery remove(db_);
                    remove.prepare("DELETE FROM tasks WHERE local_id=?"); remove.addBindValue(existing.value(0));
                    if (!remove.exec()) success=false;
                }
            }
        }
        if (success) success = db_.commit();
        if (!success) { db_.rollback(); setBusy(false); setMessage("Ошибка обновления SQLite, кэш не изменён"); return; }
        if (!etag.isEmpty()) settings_.setValue(syncKey(), etag);
        notifyTasks(); setBusy(false);
        setMessage(QString("HTTP 200: синхронизировано, задач на сервере: %1").arg(items.size()));
    }, oldTag);
}
