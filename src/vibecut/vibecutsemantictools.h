/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#pragma once

#include "vibecutmediaindex.h"

#include <QJsonObject>
#include <QList>
#include <QString>

class VibeCutToolSurface;

/** Return a copy of an embedding-store root containing only MiniLM text records
 * whose producer/model/anchor/range/source/full-text identity exactly matches
 * the supplied current canonical media documents. Stale records are excluded
 * before cosine ranking rather than merely annotated afterward. */
QJsonObject filterVibeCutCurrentSemanticTextEmbeddingRoot(const QJsonObject &root,
                                                          const QList<VibeCutMediaDocument> &documents,
                                                          int *staleSkipped = nullptr,
                                                          QString *error = nullptr);

bool registerVibeCutSemanticTools(VibeCutToolSurface &surface, QString *error = nullptr);
