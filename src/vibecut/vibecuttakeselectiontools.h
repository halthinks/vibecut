/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#pragma once

#include <QJsonObject>
#include <QString>
#include <memory>

class TimelineItemModel;
class VibeCutToolSurface;

/** Execute an already-resolved repeated-take selection plan against an
 * authoritative TimelineItemModel. The caller owns taste/review resolution;
 * this function owns destructive safety: overlap refusal, right-to-left
 * range execution, rollback, live verification and one native Undo commit.
 * Production repeated_take_selection_execute revalidates review state first
 * and then calls this same core. */
QJsonObject executeVibeCutResolvedTakeSelection(const std::shared_ptr<TimelineItemModel> &timeline,
                                                const QJsonObject &selectionPlan,
                                                const QString &removeMode);

bool registerVibeCutTakeSelectionTools(VibeCutToolSurface &surface, QString *error = nullptr);
