#include "core/LinuxUpdate.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDevice>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>
#include <QTextStream>
#include <QThread>

namespace {
int failures = 0;

void check(bool condition, const QString &message) {
    if (condition)
        return;
    QTextStream(stderr) << "FAIL: " << message << '\n';
    ++failures;
}

bool writeFile(const QString &path, const QByteArray &contents) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
           file.write(contents) == contents.size();
}

QByteArray readFile(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

void testInstallTarget() {
    const QString home = QStringLiteral("/tmp/emerald-test-home");
    check(LinuxUpdate::installTarget(QStringLiteral("/opt/Emerald.AppImage"),
                                     home) ==
              QStringLiteral("/opt/Emerald.AppImage"),
          QStringLiteral("a running AppImage updates at its current path"));
    check(LinuxUpdate::installTarget({}, home) ==
              QStringLiteral("/tmp/emerald-test-home/.local/bin/emerald"),
          QStringLiteral("a native build installs to the per-user bin folder"));
    check(LinuxUpdate::installTarget({}, {}).isEmpty(),
          QStringLiteral("an unavailable home directory fails closed"));
}

void testInstallerHelper() {
    QTemporaryDir temp(QDir::temp().filePath(
        QStringLiteral("emerald-linux-update-tests-XXXXXX")));
    check(temp.isValid(), QStringLiteral("update helper test directory exists"));
    if (!temp.isValid())
        return;

    const QString installDir = QDir(temp.path()).filePath(QStringLiteral("bin"));
    const QString stageDir = QDir(temp.path()).filePath(QStringLiteral("stage"));
    QDir().mkpath(installDir);
    QDir().mkpath(stageDir);

    const QString target = QDir(installDir).filePath(QStringLiteral("emerald"));
    const QString asset =
        QDir(stageDir).filePath(QStringLiteral("Emerald.AppImage"));
    const QString script =
        QDir(stageDir).filePath(QStringLiteral("install-emerald-update.sh"));
    const QString log = QDir(stageDir).filePath(QStringLiteral("install.log"));
    const QString marker = QDir(temp.path()).filePath(QStringLiteral("started"));

    const QByteArray oldImage = QByteArrayLiteral("#!/bin/sh\nexit 0\n");
    const QByteArray newImage =
        QByteArrayLiteral("#!/bin/sh\nprintf installed > '") +
        marker.toUtf8() + QByteArrayLiteral("'\n");
    check(writeFile(target, oldImage) && writeFile(asset, newImage) &&
              writeFile(script, LinuxUpdate::installerScript()),
          QStringLiteral("update helper fixtures are written"));
    QFile::setPermissions(target, QFileDevice::ReadOwner |
                                      QFileDevice::WriteOwner |
                                      QFileDevice::ExeOwner);
    QFile::setPermissions(script, QFileDevice::ReadOwner |
                                      QFileDevice::WriteOwner |
                                      QFileDevice::ExeOwner);

    QProcess helper;
    helper.start(QStringLiteral("/bin/sh"),
                 {script, QStringLiteral("99999999"), asset, target,
                  QStringLiteral("2.2.2-dev.2"), log});
    check(helper.waitForFinished(5000) &&
              helper.exitStatus() == QProcess::NormalExit &&
              helper.exitCode() == 0,
          QStringLiteral("detached Linux installer logic completes"));

    QElapsedTimer wait;
    wait.start();
    while (!QFile::exists(marker) && wait.elapsed() < 2000)
        QThread::msleep(10);

    check(readFile(target) == newImage,
          QStringLiteral("verified image replaces the previous executable"));
    check(QFileInfo(target).permission(QFileDevice::ExeOwner),
          QStringLiteral("installed image is executable"));
    check(readFile(marker) == QByteArrayLiteral("installed"),
          QStringLiteral("installed image is relaunched"));
    check(!QFile::exists(asset) && !QFile::exists(script) &&
              !QFile::exists(target + QStringLiteral(".update-new")) &&
              !QFile::exists(target + QStringLiteral(".previous-update")),
          QStringLiteral("successful installation removes staging and backup files"));
    check(!QFile::exists(log) && !QFileInfo::exists(stageDir),
          QStringLiteral("successful installation removes its temporary directory"));
}

void testInstallerFailureKeepsExistingExecutable() {
    QTemporaryDir temp(QDir::temp().filePath(
        QStringLiteral("emerald-linux-update-failure-tests-XXXXXX")));
    check(temp.isValid(), QStringLiteral("failure test directory exists"));
    if (!temp.isValid())
        return;

    const QString target =
        QDir(temp.path()).filePath(QStringLiteral("emerald"));
    const QString missingAsset =
        QDir(temp.path()).filePath(QStringLiteral("missing.AppImage"));
    const QString script =
        QDir(temp.path()).filePath(QStringLiteral("install-update.sh"));
    const QString log =
        QDir(temp.path()).filePath(QStringLiteral("install.log"));
    const QString marker =
        QDir(temp.path()).filePath(QStringLiteral("old-version-started"));
    const QByteArray oldImage =
        QByteArrayLiteral("#!/bin/sh\nprintf old > '") + marker.toUtf8() +
        QByteArrayLiteral("'\n");

    check(writeFile(target, oldImage) &&
              writeFile(script, LinuxUpdate::installerScript()),
          QStringLiteral("failure fixtures are written"));
    QFile::setPermissions(target, QFileDevice::ReadOwner |
                                      QFileDevice::WriteOwner |
                                      QFileDevice::ExeOwner);

    QProcess helper;
    helper.start(QStringLiteral("/bin/sh"),
                 {script, QStringLiteral("99999999"), missingAsset, target,
                  QStringLiteral("2.2.2-dev.2"), log});
    check(helper.waitForFinished(5000) && helper.exitCode() != 0,
          QStringLiteral("missing verified asset fails installation"));

    QElapsedTimer wait;
    wait.start();
    while (!QFile::exists(marker) && wait.elapsed() < 2000)
        QThread::msleep(10);

    check(readFile(target) == oldImage &&
              readFile(marker) == QByteArrayLiteral("old"),
          QStringLiteral(
              "failure preserves and relaunches the existing executable"));
    check(readFile(log).contains("Missing verified update"),
          QStringLiteral("failure retains a useful diagnostic log"));
}
} // namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    testInstallTarget();
    testInstallerHelper();
    testInstallerFailureKeepsExistingExecutable();
    if (failures == 0)
        QTextStream(stdout) << "All Linux update tests passed.\n";
    return failures == 0 ? 0 : 1;
}
