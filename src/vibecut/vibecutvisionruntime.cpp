/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecutvisionruntime.h"

#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

namespace {
const QString kTransformersVersion = QStringLiteral("5.16.1");
const QString kTorchVersion = QStringLiteral("2.14.0");
const QString kTorchvisionVersion = QStringLiteral("0.29.0");
const QString kPillowVersion = QStringLiteral("12.3.0");

QString pythonOverride()
{
    return QString::fromLocal8Bit(qgetenv("VIBECUT_VISION_PYTHON")).trimmed();
}
}

QString vibeCutVisionVenvDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/vibecut-vision-venv");
}

QString vibeCutVisionPython()
{
    const QString overridePath = pythonOverride();
    return overridePath.isEmpty() ? vibeCutVisionVenvDir() + QStringLiteral("/bin/python3") : overridePath;
}

QString vibeCutVisionRequirements()
{
    return QStandardPaths::locate(QStandardPaths::AppDataLocation,
                                  QStringLiteral("scripts/vibecut/requirements-vision.txt"));
}

bool vibeCutVisionDependenciesReady(QString *error)
{
    if (error) error->clear();
    const QString python = vibeCutVisionPython();
    if (python.isEmpty() || !QFileInfo::exists(python)) {
        if (error) *error = QStringLiteral("Vision Python environment is missing. Run VibeCut vision setup first or set VIBECUT_VISION_PYTHON.");
        return false;
    }
    const QString requirements = vibeCutVisionRequirements();
    if (requirements.isEmpty() || !QFileInfo::exists(requirements)) {
        if (error) *error = QStringLiteral("Pinned VibeCut vision requirements file is not installed.");
        return false;
    }

    QProcess probe;
    probe.start(python, {QStringLiteral("-c"),
                         QStringLiteral("import transformers,torch,torchvision,PIL; print(transformers.__version__); print(torch.__version__); print(torchvision.__version__); print(PIL.__version__)")});
    if (!probe.waitForStarted(2000) || !probe.waitForFinished(8000) || probe.exitStatus() != QProcess::NormalExit || probe.exitCode() != 0) {
        if (error) *error = QStringLiteral("Configured Python cannot import the pinned VibeCut vision runtime.");
        return false;
    }
    const QStringList lines = QString::fromUtf8(probe.readAllStandardOutput()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    if (lines.size() < 4 || lines.at(0).trimmed() != kTransformersVersion ||
        !lines.at(1).trimmed().startsWith(kTorchVersion) ||
        !lines.at(2).trimmed().startsWith(kTorchvisionVersion) ||
        lines.at(3).trimmed() != kPillowVersion) {
        if (error) *error = QStringLiteral("Vision runtime version mismatch: requires Transformers %1, Torch %2.x, Torchvision %3.x and Pillow %4.")
                               .arg(kTransformersVersion, kTorchVersion, kTorchvisionVersion, kPillowVersion);
        return false;
    }
    return true;
}
