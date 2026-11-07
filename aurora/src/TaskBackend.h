#pragma once

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSettings>
#include <QSqlDatabase>
#include <QVariantList>
#include <QTimer>
#include <QJsonObject>
#include <functional>

class TaskBackend : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString serverUrl READ serverUrl NOTIFY configChanged)
    Q_PROPERTY(QString fingerprint READ fingerprint NOTIFY configChanged)
    Q_PROPERTY(QString username READ username NOTIFY authChanged)
    Q_PROPERTY(bool authenticated READ authenticated NOTIFY authChanged)
    Q_PROPERTY(bool offline READ offline WRITE setOffline NOTIFY offlineChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)
    Q_PROPERTY(QVariantList tasks READ tasks NOTIFY tasksChanged)
public:
    explicit TaskBackend(QObject *parent = nullptr);
    ~TaskBackend();

    QString serverUrl() const { return serverUrl_; }
    QString fingerprint() const { return fingerprint_; }
    QString username() const { return username_; }
    bool authenticated() const { return !token_.isEmpty(); }
    bool offline() const { return offline_; }
    bool busy() const { return busy_; }
    QString message() const { return message_; }
    QVariantList tasks() const;

    Q_INVOKABLE void configure(const QString &url, const QString &sha256Hex);
    Q_INVOKABLE void login(const QString &name, const QString &password);
    Q_INVOKABLE void logout();
    Q_INVOKABLE void setOffline(bool enabled);
    Q_INVOKABLE void sync();
    Q_INVOKABLE void createTask(const QString &title, const QString &description,
                                const QString &status, const QString &dueDate);
    Q_INVOKABLE void updateTask(int localId, const QString &title, const QString &description,
                                const QString &status, const QString &dueDate);
    Q_INVOKABLE void deleteTask(int localId);

signals:
    void configChanged();
    void authChanged();
    void offlineChanged();
    void busyChanged();
    void messageChanged();
    void tasksChanged();

private:
    using Callback = std::function<void(int, const QByteArray &, const QString &, const QByteArray &)>;
    void request(const QByteArray &method, const QString &path, const QJsonObject &body,
                 const Callback &done, const QByteArray &ifNoneMatch = QByteArray());
    void setBusy(bool value);
    void setMessage(const QString &message);
    void notifyTasks();
    void syncNext();
    void downloadTasks();
    bool initDb();
    QString syncKey() const;
    QString normalizeDueDate(const QString &date) const;
    bool canEdit() const;

    QNetworkAccessManager network_;
    QSettings settings_;
    QSqlDatabase db_;
    QTimer periodicSync_;
    QString serverUrl_;
    QString fingerprint_;
    QString username_;
    QString token_;
    QString message_;
    bool offline_ = false;
    bool busy_ = false;
};
