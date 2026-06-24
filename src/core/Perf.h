#pragma once

#include <QDebug>
#include <QElapsedTimer>
#include <QLoggingCategory>
#include <QString>
#include <QtGlobal>

Q_DECLARE_LOGGING_CATEGORY(emeraldPerf)

class PerfScope {
public:
    explicit PerfScope(const char *name) : m_name(QString::fromLatin1(name)) {
        if (enabled())
            m_timer.start();
    }

    ~PerfScope() {
        if (enabled())
            qCDebug(emeraldPerf).noquote()
                << m_name << QStringLiteral("%1 ms").arg(m_timer.elapsed());
    }

    static bool enabled() {
        static const bool on = qEnvironmentVariableIsSet("EMERALD_PROFILE");
        return on;
    }

private:
    QString m_name;
    QElapsedTimer m_timer;
};

#define EMERALD_CONCAT_IMPL(a, b) a##b
#define EMERALD_CONCAT(a, b) EMERALD_CONCAT_IMPL(a, b)
#define EMERALD_PROFILE_SCOPE(name) PerfScope EMERALD_CONCAT(perfScope_, __LINE__)(name)
