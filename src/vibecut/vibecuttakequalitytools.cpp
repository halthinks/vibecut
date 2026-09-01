/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecuttakequalitytools.h"

#include "bin/projectclip.h"
#include "bin/projectitemmodel.h"
#include "core.h"
#include "vibecutmediaevidence.h"
#include "vibecuttoolsurface.h"

#include <QCryptographicHash>
#include <QFileInfo>
#include <QJsonArray>

namespace {
QJsonObject err(const QString &message)
{
    return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), message}};
}

QString statFingerprint(const QFileInfo &info)
{
    const QByteArray payload = info.canonicalFilePath().toUtf8() + '\n' + QByteArray::number(info.size()) + '\n' +
                               QByteArray::number(info.lastModified().toMSecsSinceEpoch());
    return QString::fromLatin1(QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex());
}

bool overlaps(int start, int end, int rangeStart, int rangeEnd)
{
    if (start < 0 || end < 0) return true;
    if (rangeStart < 0 || rangeEnd < 0) return true;
    return qMax(start, rangeStart) < qMin(end, rangeEnd);
}

QJsonObject quality(VibeCutToolSurface *surface, const QJsonObject &input)
{
    if (!surface || !pCore) return err(QStringLiteral("VibeCut/Kdenlive core is unavailable."));
    const QString binId = input.value(QStringLiteral("bin_id")).toString().trimmed();
    if (binId.isEmpty()) return err(QStringLiteral("bin_id must not be empty."));
    const std::shared_ptr<ProjectItemModel> model = pCore->projectItemModel();
    const std::shared_ptr<ProjectClip> clip = model ? model->getClipByBinID(binId) : nullptr;
    if (!clip) return err(QStringLiteral("Bin clip '%1' does not exist.").arg(binId));
    if (!clip->hasUrl()) return err(QStringLiteral("Take quality context currently requires a file-backed source."));
    const QFileInfo info(clip->url());
    if (!info.exists() || !info.isFile()) return err(QStringLiteral("Source file is missing or invalid."));

    const int duration = qMax(0, clip->getFramePlaytime());
    const int startFrame = input.contains(QStringLiteral("start_frame")) ? input.value(QStringLiteral("start_frame")).toInt(-1) : 0;
    const int endFrame = input.contains(QStringLiteral("end_frame")) ? input.value(QStringLiteral("end_frame")).toInt(-1) : duration;
    if (startFrame < 0 || endFrame <= startFrame || endFrame > duration) {
        return err(QStringLiteral("Range must satisfy 0 <= start_frame < end_frame <= %1.").arg(duration));
    }

    const QString sourceId = QStringLiteral("bin:%1").arg(binId);
    const QString fingerprint = statFingerprint(info);
    QString evidenceError;
    const QJsonArray records = VibeCutMediaEvidence::loadCurrent(&evidenceError);
    if (!evidenceError.isEmpty()) return err(evidenceError);

    QJsonObject loudness;
    QJsonObject blur;
    QJsonArray silence;
    QJsonArray black;
    QJsonArray freeze;
    QJsonArray shots;
    QJsonArray other;
    for (const QJsonValue &value : records) {
        const QJsonObject object = value.toObject();
        if (object.value(QStringLiteral("source_id")).toString() != sourceId ||
            object.value(QStringLiteral("source_fingerprint")).toString() != fingerprint) continue;
        const QString kind = object.value(QStringLiteral("kind")).toString();
        const int recordStart = object.value(QStringLiteral("start_frame")).toInt(-1);
        const int recordEnd = object.value(QStringLiteral("end_frame")).toInt(-1);
        if (!overlaps(startFrame, endFrame, recordStart, recordEnd)) continue;

        if (kind == QLatin1String("loudness_summary")) loudness = object;
        else if (kind == QLatin1String("blur_summary")) blur = object;
        else if (kind == QLatin1String("silence")) silence.append(object);
        else if (kind == QLatin1String("black_frame_range")) black.append(object);
        else if (kind == QLatin1String("freeze_frame_range")) freeze.append(object);
        else if (kind == QLatin1String("shot_boundary")) shots.append(object);
        else other.append(object);
    }

    qint64 silenceFrames = 0;
    auto overlappedFrames = [startFrame, endFrame](const QJsonArray &ranges) {
        qint64 total = 0;
        for (const QJsonValue &value : ranges) {
            const QJsonObject object = value.toObject();
            const int a = qMax(startFrame, object.value(QStringLiteral("start_frame")).toInt(startFrame));
            const int b = qMin(endFrame, object.value(QStringLiteral("end_frame")).toInt(endFrame));
            if (b > a) total += b - a;
        }
        return total;
    };
    silenceFrames = overlappedFrames(silence);
    const qint64 blackFrames = overlappedFrames(black);
    const qint64 freezeFrames = overlappedFrames(freeze);

    const QJsonObject freshness = surface->invoke(QStringLiteral("media_evidence_freshness"), QJsonObject{{QStringLiteral("bin_id"), binId}});
    const double fps = pCore->getCurrentFps();
    return QJsonObject{{QStringLiteral("ok"), true},
                       {QStringLiteral("bin_id"), binId},
                       {QStringLiteral("source_id"), sourceId},
                       {QStringLiteral("source_fingerprint"), fingerprint},
                       {QStringLiteral("start_frame"), startFrame}, {QStringLiteral("end_frame"), endFrame},
                       {QStringLiteral("duration_frames"), endFrame - startFrame},
                       {QStringLiteral("loudness"), loudness}, {QStringLiteral("blur"), blur},
                       {QStringLiteral("silence_ranges"), silence}, {QStringLiteral("black_ranges"), black},
                       {QStringLiteral("freeze_ranges"), freeze}, {QStringLiteral("shot_boundaries"), shots},
                       {QStringLiteral("other_evidence"), other},
                       {QStringLiteral("silence_overlap_frames"), silenceFrames},
                       {QStringLiteral("black_overlap_frames"), blackFrames},
                       {QStringLiteral("freeze_overlap_frames"), freezeFrames},
                       {QStringLiteral("silence_overlap_seconds"), fps > 0.0 ? silenceFrames / fps : 0.0},
                       {QStringLiteral("black_overlap_seconds"), fps > 0.0 ? blackFrames / fps : 0.0},
                       {QStringLiteral("freeze_overlap_seconds"), fps > 0.0 ? freezeFrames / fps : 0.0},
                       {QStringLiteral("freshness"), freshness},
                       {QStringLiteral("note"), QStringLiteral("Measured evidence context only. VibeCut does not collapse blur, loudness, silence, black/freeze frames or shot structure into an arbitrary editorial quality score.")}};
}
} // namespace

bool registerVibeCutTakeQualityTools(VibeCutToolSurface &surface, QString *error)
{
    const QJsonObject input{{QStringLiteral("type"), QStringLiteral("object")},
                            {QStringLiteral("properties"), QJsonObject{
                                {QStringLiteral("bin_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                                {QStringLiteral("start_frame"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 0}}},
                                {QStringLiteral("end_frame"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 1}}}}},
                            {QStringLiteral("required"), QJsonArray{QStringLiteral("bin_id")}},
                            {QStringLiteral("additionalProperties"), false}};
    VibeCutToolPolicy policy;
    policy.name = QStringLiteral("take_quality_context");
    policy.risk = VibeCutToolRisk::ReadOnly;
    return surface.registerTool(QJsonObject{{QStringLiteral("name"), policy.name},
                                            {QStringLiteral("description"), QStringLiteral("Return current measured media-quality evidence for a file-backed bin asset or exact source-frame range: loudness/clipping, blur, silence, black/freeze overlap, shot boundaries and evidence freshness. Does not invent a single best-take score.")},
                                            {QStringLiteral("input_schema"), input}},
                                policy, [&surface](const QJsonObject &input) { return quality(&surface, input); }, error);
}
