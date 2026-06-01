#pragma once
// LapeIPC.h — komunikacja z procesem Lape przez Unix domain socket
// Protokół: JSON przez QLocalSocket
// Socket: ~/.config/lape/bridge/lape.sock
//
// Komendy wysyłane do Lape:
//   { "cmd": "open_file",       "path": "/foo/bar.jpg" }
//   { "cmd": "open_as_layer",   "path": "/foo/bar.jpg" }
//   { "cmd": "open_raw_acr",    "path": "/foo/bar.cr2" }   // ACR equivalent
//   { "cmd": "batch_open",      "paths": [...] }
//
// Odpowiedzi od Lape:
//   { "status": "ok" }
//   { "status": "error", "msg": "..." }

#include <QObject>
#include <QString>
#include <QStringList>
#include <QLocalSocket>

namespace LapesEye {

enum class IPCStatus {
    Connected,
    Disconnected,
    Error
};

class LapeIPC : public QObject {
    Q_OBJECT

public:
    explicit LapeIPC(QObject* parent = nullptr);
    ~LapeIPC();

    // Próba połączenia z działającym procesem Lape
    void connect_to_lape();
    void disconnect();

    IPCStatus status() const { return m_status; }
    bool is_connected() const { return m_status == IPCStatus::Connected; }

    // Wysyłanie komend (bezpieczne — kolejkuje gdy brak połączenia)
    void open_file(const QString& path);
    void open_as_layer(const QString& path);
    void open_raw(const QString& path);          // odpowiednik ACR
    void batch_open(const QStringList& paths);

signals:
    void status_changed(IPCStatus status);
    void lape_responded(const QString& response);

private slots:
    void on_connected();
    void on_disconnected();
    void on_error(QLocalSocket::LocalSocketError err);
    void on_ready_read();

private:
    void send_json(const QByteArray& json);
    static QString socket_path();

    QLocalSocket*     m_socket  = nullptr;
    IPCStatus         m_status  = IPCStatus::Disconnected;
    QList<QByteArray> m_queue;   // bufor komend gdy brak połączenia
};

} // namespace LapesEye
