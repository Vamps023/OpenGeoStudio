#pragma once

// ============================================================
// Logger — Scoped logging with levels
// ============================================================
//
// Replaces the TypeScript Logger (core/logger/logger.ts).
// Uses Qt's qDebug for console output, with optional file transport.
//
// This is the application's centralized logging facility. Prefer
// Logger over raw qDebug() calls so all output shares a consistent
// format (timestamp, level, scope) and a single configuration point
// (file transport, level filtering).
//
// Usage:
//   appLog().info("Project opened:", name);
//   Logger scoped = appLog().child("osm");
//

#include <QDebug>
#include <QFile>
#include <QTextStream>
#include <QString>
#include <QDateTime>
#include <memory>

class Logger {
public:
    enum class Level { Debug, Info, Warn, Error };

    explicit Logger(const QString& scope) : m_scope(scope) {}

    void setLevel(Level level) { m_level = level; }

    template<typename... Args>
    void debug(const Args&... args) const {
        if (m_level <= Level::Debug) log("DEBUG", concat(args...));
    }

    template<typename... Args>
    void info(const Args&... args) const {
        if (m_level <= Level::Info) log("INFO", concat(args...));
    }

    template<typename... Args>
    void warn(const Args&... args) const {
        if (m_level <= Level::Warn) log("WARN", concat(args...));
    }

    template<typename... Args>
    void error(const Args&... args) const {
        if (m_level <= Level::Error) log("ERROR", concat(args...));
    }

    Logger child(const QString& subscope) const {
        return Logger(m_scope + "::" + subscope);
    }

    static void addFileTransport(const QString& path) {
        s_fileTransport = std::make_unique<QFile>(path);
        if (s_fileTransport->open(QIODevice::Append | QIODevice::Text)) {
            s_fileStream = std::make_unique<QTextStream>(s_fileTransport.get());
        }
    }

private:
    QString m_scope;
    Level m_level = Level::Debug;

    static std::unique_ptr<QFile> s_fileTransport;
    static std::unique_ptr<QTextStream> s_fileStream;

    void log(const char* level, const QString& msg) const {
        const QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz");
        const QString line = QString("[%1] [%2] [%3] %4").arg(timestamp, level, m_scope, msg);
        qDebug().noquote() << line;
        if (s_fileStream) {
            *s_fileStream << line << "\n";
            s_fileStream->flush();
        }
    }

    // Concatenate arguments into a single QString
    template<typename T>
    static QString toStr(const T& v) {
        if constexpr (std::is_same_v<std::decay_t<T>, QString>) {
            return v;
        } else if constexpr (std::is_same_v<std::decay_t<T>, const char*> || std::is_same_v<std::decay_t<T>, char*>) {
            return QString::fromUtf8(v);
        } else if constexpr (std::is_integral_v<std::decay_t<T>> || std::is_floating_point_v<std::decay_t<T>>) {
            return QString::number(v);
        } else {
            return QString::fromStdString(std::to_string(v));
        }
    }

    template<typename... Args>
    static QString concat(const Args&... args) {
        QString result;
        (result.append(toStr(args)).append(" "), ...);
        result.chop(1); // remove trailing space
        return result;
    }
};

inline std::unique_ptr<QFile> Logger::s_fileTransport;
inline std::unique_ptr<QTextStream> Logger::s_fileStream;

// ─── Global accessor ───
// Returns the application-wide default logger. Header-only modules
// (OSM pipeline, world model, terrain) use this instead of qDebug()
// so all output flows through the centralized logger.
inline Logger& appLog() {
    static Logger logger("app");
    return logger;
}
