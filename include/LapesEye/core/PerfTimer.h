#pragma once
// ─── Prosty profiler wydajności — włącz przez #define LAPE_PERF 1 ────────────
// Użycie:
//   PERF_START("sync_canvas");
//   ... kod ...
//   PERF_END("sync_canvas");   // drukuje czas w ms
//   PERF_FRAME("paintEvent");  // zlicza wywołania + łączny czas per sekundę

#include <QElapsedTimer>
#include <QDebug>
#include <QHash>
#include <QString>

// Ustaw na 1 żeby włączyć pomiary, 0 żeby wyłączyć (zero overhead)
#define LAPE_PERF 1

#if LAPE_PERF

struct LapePerfCounter {
    qint64 total_us = 0;
    int    calls    = 0;
    qint64 max_us   = 0;
    void record(qint64 us) {
        total_us += us;
        calls++;
        if (us > max_us) max_us = us;
    }
    void print(const QString& name) const {
        if (calls == 0) return;
        qDebug() << QString("[PERF] %-30s  calls=%1  avg=%2µs  max=%3µs  total=%4ms")
                        .arg(name).arg(calls)
                        .arg(total_us / calls)
                        .arg(max_us)
                        .arg(total_us / 1000);
    }
};

// Globalny rejestr — dostępny z każdego pliku
inline QHash<QString, LapePerfCounter>& lape_perf_registry() {
    static QHash<QString, LapePerfCounter> reg;
    return reg;
}

struct LapeScopedTimer {
    QString      m_name;
    QElapsedTimer m_t;
    LapeScopedTimer(const QString& name) : m_name(name) { m_t.start(); }
    ~LapeScopedTimer() {
        qint64 us = m_t.nsecsElapsed() / 1000;
        lape_perf_registry()[m_name].record(us);
        // Drukuj tylko gdy czas > 5ms żeby nie zaśmiecać konsoli
        if (us > 5000)
            qDebug() << QString("[PERF] %1: %2ms").arg(m_name).arg(us / 1000.0, 0, 'f', 1);
    }
};

#define PERF_SCOPE(name)  LapeScopedTimer _perf_##__LINE__(name)
#define PERF_PRINT_ALL()  for (auto it = lape_perf_registry().begin(); \
                               it != lape_perf_registry().end(); ++it) \
                              it.value().print(it.key())

#else
// LAPE_PERF == 0 — zero overhead
#define PERF_SCOPE(name)  do {} while(0)
#define PERF_PRINT_ALL()  do {} while(0)
#endif
