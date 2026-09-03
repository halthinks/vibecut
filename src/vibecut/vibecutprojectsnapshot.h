/*
    SPDX-FileCopyrightText: 2026 vibecut contributors
    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/
#pragma once

#include <QJsonObject>
#include <QString>
#include <QtGlobal>
#include <memory>

class TimelineItemModel;

struct VibeCutProjectDiff {
    qint64 revisionDelta = 0;
    int durationFramesDelta = 0;
    int clipsDelta = 0;
    int tracksDelta = 0;
    int subtitlesDelta = 0;
    int effectsDelta = 0;

    QJsonObject toJson() const;
    QString summary() const;
};

struct VibeCutProjectSnapshot {
    quint64 revision = 0;
    int durationFrames = 0;
    int clips = 0;
    int tracks = 0;
    int subtitles = 0;
    int effects = 0;
    bool available = false;

    QJsonObject toJson() const;
    VibeCutProjectDiff diffTo(const VibeCutProjectSnapshot &after) const;
    static VibeCutProjectSnapshot capture(quint64 revision);

    /**
     * Capture a deterministic, revision-independent mutation state suitable for
     * verified-success and Undo/Redo fidelity evaluation.
     *
     * Schema v1 includes timeline topology/order, track state/effects, clip
     * identity/source span/timing/speed/effects, compositions and parameters,
     * groups, subtitles and master effects. The project revision is deliberately
     * excluded because Undo/Redo may legitimately advance revision counters while
     * restoring the same editable project state.
     */
    static QJsonObject captureMutationStateV1();

    /** Model-bound form used by headless golden tests and any caller that already
     * owns the authoritative TimelineItemModel. This is the canonical
     * implementation; captureMutationStateV1() only resolves the active model. */
    static QJsonObject mutationStateV1(const std::shared_ptr<TimelineItemModel> &model);
};
