/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecutextractorrequest.h"

#include "bin/projectclip.h"
#include "bin/projectitemmodel.h"
#include "core.h"

#include <QCryptographicHash>
#include <QFileInfo>

namespace {
QString statFingerprint(const QFileInfo &info)
{
    const QByteArray payload = info.canonicalFilePath().toUtf8() + '\n' +
                               QByteArray::number(info.size()) + '\n' +
                               QByteArray::number(info.lastModified().toMSecsSinceEpoch());
    return QString::fromLatin1(QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex());
}
}

bool normalizeVibeCutExtractorRequest(const QString &capability,
                                      const QJsonObject &request,
                                      QJsonObject &normalized,
                                      QString *error)
{
    if (error) error->clear();
    normalized = QJsonObject();
    if (!pCore) {
        if (error) *error = QStringLiteral("Kdenlive core is unavailable.");
        return false;
    }
    const QString binId = request.value(QStringLiteral("bin_id")).toString().trimmed();
    if (binId.isEmpty()) {
        if (error) *error = QStringLiteral("Provider extractor request requires bin_id.");
        return false;
    }
    const std::shared_ptr<ProjectItemModel> model = pCore->projectItemModel();
    const std::shared_ptr<ProjectClip> clip = model ? model->getClipByBinID(binId) : nullptr;
    if (!clip) {
        if (error) *error = QStringLiteral("Bin clip '%1' does not exist.").arg(binId);
        return false;
    }
    if (!clip->hasUrl()) {
        if (error) *error = QStringLiteral("Provider extractor request currently requires a file-backed source.");
        return false;
    }
    const QFileInfo info(clip->url());
    if (!info.exists() || !info.isFile()) {
        if (error) *error = QStringLiteral("Source file is missing or invalid.");
        return false;
    }
    const double fps = pCore->getCurrentFps();
    if (fps <= 0.0) {
        if (error) *error = QStringLiteral("Current project frame rate is invalid.");
        return false;
    }
    const int durationFrames = qMax(0, clip->getFramePlaytime());
    const int startFrame = request.contains(QStringLiteral("start_frame")) ? request.value(QStringLiteral("start_frame")).toInt(-1) : 0;
    const int endFrame = request.contains(QStringLiteral("end_frame")) ? request.value(QStringLiteral("end_frame")).toInt(-1) : durationFrames;
    if (startFrame < 0 || endFrame < startFrame || endFrame > durationFrames) {
        if (error) *error = QStringLiteral("Extractor frame bounds must satisfy 0 <= start_frame <= end_frame <= duration_frames (%1).").arg(durationFrames);
        return false;
    }

    QJsonObject parameters = request;
    parameters.remove(QStringLiteral("bin_id"));
    parameters.remove(QStringLiteral("start_frame"));
    parameters.remove(QStringLiteral("end_frame"));

    normalized = QJsonObject{{QStringLiteral("capability"), capability.trimmed().toLower()},
                             {QStringLiteral("bin_id"), binId},
                             {QStringLiteral("source_id"), QStringLiteral("bin:%1").arg(binId)},
                             {QStringLiteral("source_path"), info.canonicalFilePath()},
                             {QStringLiteral("source_fingerprint"), statFingerprint(info)},
                             {QStringLiteral("fps"), fps},
                             {QStringLiteral("duration_frames"), durationFrames},
                             {QStringLiteral("start_frame"), startFrame},
                             {QStringLiteral("end_frame"), endFrame},
                             {QStringLiteral("has_audio"), clip->hasAudio()},
                             {QStringLiteral("has_video"), clip->hasVideo()},
                             {QStringLiteral("parameters"), parameters}};
    return true;
}
