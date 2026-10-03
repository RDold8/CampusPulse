#include "adapters/ArtifactWriter.h"
#include <QFile>
#include <QDir>
#include <QSaveFile>
#include <QUuid>
#include <QScopeGuard>
#include <QDebug>
#include <stdexcept>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif
namespace campus {
void writeArtifact(const QString &path, const QByteArray &bytes) {
#ifdef Q_OS_WIN
    const auto staging = path + "." + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".tmp";
    const auto cleanup = qScopeGuard([&] { QFile::remove(staging); });
    QFile file(staging);
    if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly) ||
        file.write(bytes) != bytes.size() || !file.flush())
        throw std::runtime_error(
            ("文件写入失败：" + path + " · " + file.errorString()).toStdString());
    file.close();
    const auto from = QDir::toNativeSeparators(staging), to = QDir::toNativeSeparators(path);
    const auto move = [&](DWORD flags) {
        return MoveFileExW(reinterpret_cast<LPCWSTR>(from.utf16()),
                           reinterpret_cast<LPCWSTR>(to.utf16()), flags);
    };
    constexpr DWORD flags = MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH;
    if (!move(flags)) {
        const auto error = GetLastError();
        if (error != ERROR_NOT_SAME_DEVICE || !move(flags | MOVEFILE_COPY_ALLOWED))
            throw std::runtime_error(
                ("文件提交失败：" + path + " · Windows错误 " + QString::number(GetLastError()))
                    .toStdString());
        qWarning() << "文件提交使用Windows复制移动（非原子）:" << path;
    }
#else
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        throw std::runtime_error(
            ("文件写入失败：" + path + " · " + file.errorString()).toStdString());
#endif
}
} // namespace campus
