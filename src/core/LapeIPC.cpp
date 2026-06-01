#include "LapesEye/core/LapeIPC.h"

#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QStandardPaths>
#include <QTimer>

namespace LapesEye {

LapeIPC::LapeIPC(QObject* parent)
    : QObject(parent)
    , m_socket(new QLocalSocket(this))
{
    QObject::connect(m_socket, &QLocalSocket::connected,
                     this, &LapeIPC::on_connected);
    QObject::connect(m_socket, &QLocalSocket::disconnected,
                     this, &LapeIPC::on_disconnected);
    QObject::connect(m_socket, &QLocalSocket::errorOccurred,
                     this, &LapeIPC::on_error);
    QObject::connect(m_socket, &QLocalSocket::readyRead,
                     this, &LapeIPC::on_ready_read);
}

LapeIPC::~LapeIPC() {
    disconnect();
}

QString LapeIPC::socket_path() {
    // ~/.config/lape/bridge/lape.sock
    QString cfg = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    QString dir = cfg + "/lape/bridge";
    QDir().mkpath(dir);
    return dir + "/lape.sock";
}

void LapeIPC::connect_to_lape() {
    if (m_socket->state() != QLocalSocket::UnconnectedState)
        return;
    m_socket->connectToServer(socket_path());
}

void LapeIPC::disconnect() {
    m_socket->disconnectFromServer();
}

void LapeIPC::on_connected() {
    m_status = IPCStatus::Connected;
    emit status_changed(m_status);

    // Flush queued commands
    for (const auto& cmd : m_queue)
        m_socket->write(cmd);
    m_queue.clear();
}

void LapeIPC::on_disconnected() {
    m_status = IPCStatus::Disconnected;
    emit status_changed(m_status);

    // Auto-reconnect po 3s
    QTimer::singleShot(3000, this, &LapeIPC::connect_to_lape);
}

void LapeIPC::on_error(QLocalSocket::LocalSocketError) {
    m_status = IPCStatus::Error;
    emit status_changed(m_status);
    QTimer::singleShot(5000, this, &LapeIPC::connect_to_lape);
}

void LapeIPC::on_ready_read() {
    QByteArray data = m_socket->readAll();
    emit lape_responded(QString::fromUtf8(data));
}

void LapeIPC::send_json(const QByteArray& json) {
    if (m_status == IPCStatus::Connected) {
        m_socket->write(json + "\n");
    } else {
        m_queue.append(json + "\n");
        connect_to_lape();
    }
}

void LapeIPC::open_file(const QString& path) {
    QJsonObject obj;
    obj["cmd"]  = "open_file";
    obj["path"] = path;
    send_json(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

void LapeIPC::open_as_layer(const QString& path) {
    QJsonObject obj;
    obj["cmd"]  = "open_as_layer";
    obj["path"] = path;
    send_json(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

void LapeIPC::open_raw(const QString& path) {
    QJsonObject obj;
    obj["cmd"]  = "open_raw_acr";
    obj["path"] = path;
    send_json(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

void LapeIPC::batch_open(const QStringList& paths) {
    QJsonObject obj;
    obj["cmd"] = "batch_open";
    QJsonArray arr;
    for (const auto& p : paths) arr.append(p);
    obj["paths"] = arr;
    send_json(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

} // namespace LapesEye
