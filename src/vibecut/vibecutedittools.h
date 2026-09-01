/*
    SPDX-FileCopyrightText: 2026 vibecut contributors
    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/
#pragma once

#include "timeline2/model/undohelper.hpp"

#include <QJsonObject>
#include <QString>
#include <QVector>
#include <memory>

class TimelineItemModel;
class VibeCutToolSurface;

/** Append one timeline-wide range removal to an existing native Kdenlive
 * undo/redo transaction. An empty track list means every timeline track.
 * The operation fails closed on locked/unknown tracks and verifies the live
 * postcondition before returning success. */
bool appendVibeCutTimelineRangeRemove(const std::shared_ptr<TimelineItemModel> &model, int startFrame, int endFrame, bool liftOnly,
                                      const QVector<int> &trackIds, Fun &undo, Fun &redo, QJsonObject *verification = nullptr,
                                      QString *error = nullptr);

/** Register core undoable timeline-edit primitives backed by Kdenlive's own
 * TimelineModel / TimelineFunctions request APIs. */
bool registerVibeCutEditTools(VibeCutToolSurface &surface, QString *error = nullptr);
