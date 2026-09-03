/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecutsemanticruntime.h"

#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

namespace {
const QString kSentenceTransformersVersion = QStringLiteral("6.0.1");
const QString kTransformersVersion = QStringLiteral("5.16.1");
const QString kTorchVersion = QStringLiteral("2.14.0");

QString pythonOverride()
{
    return QString::fromLocal8Bit(qgetenv("VIBECUT_SEMANTIC_PYTHON")).trimmed();
}
}

QString vibeCutSemanticVenvDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/vibecut-semantic-venv");
}

QString vibeCutSemanticPython()
{
    const QString overridePath = pythonOverride();
    return overridePath.isEmpty() ? vibeCutSemanticVenvDir() + QStringLiteral("/bin/python3") : overridePath;
}

QString vibeCutSemanticScript()
{
    return QStandardPaths::locate(QStandardPaths::AppDataLocation,
                                  QStringLiteral("scripts/vibecut/semantic_minilm.py"));
}

QString vibeCutSemanticRequirements()
{
    return QStandardPaths::locate(QStandardPaths::AppDataLocation,
                                  QStringLiteral("scripts/vibecut/requirements-semantic.txt"));
}

bool vibeCutSemanticDependenciesReady(QString *error)
{
    if (error) error->clear();
    const QString python = vibeCutSemanticPython();
    const QString script = vibeCutSemanticScript();
    const QString requirements = vibeCutSemanticRequirements();
    if (python.isEmpty() || !QFileInfo::exists(python)) {
        if (error) *error = QStringLiteral("Semantic Python environment is missing. Run VibeCut semantic setup first or set VIBECUT_SEMANTIC_PYTHON.");
        return false;
    }
    if (script.isEmpty() || !QFileInfo::exists(script) || requirements.isEmpty() || !QFileInfo::exists(requirements)) {
        if (error) *error = QStringLiteral("Pinned VibeCut semantic helper/requirements are not installed.");
        return false;
    }

    QProcess probe;
    probe.start(python, {QStringLiteral("-c"),
                         QStringLiteral("import sentence_transformers,transformers,torch; print(sentence_transformers.__version__); print(transformers.__version__); print(torch.__version__)")});
    if (!probe.waitForStarted(2000) || !probe.waitForFinished(8000) ||
        probe.exitStatus() != QProcess::NormalExit || probe.exitCode() != 0) {
        if (error) *error = QStringLiteral("Configured Python cannot import the pinned VibeCut semantic runtime.");
        return false;
    }
    const QStringList lines = QString::fromUtf8(probe.readAllStandardOutput()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    if (lines.size() < 3 || lines.at(0).trimmed() != kSentenceTransformersVersion ||
        lines.at(1).trimmed() != kTransformersVersion || !lines.at(2).trimmed().startsWith(kTorchVersion)) {
        if (error) *error = QStringLiteral("Semantic runtime version mismatch: requires Sentence Transformers %1, Transformers %2 and Torch %3.x.")
                               .arg(kSentenceTransformersVersion, kTransformersVersion, kTorchVersion);
        return false;
    }
    return true;
}
