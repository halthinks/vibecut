/*
    SPDX-FileCopyrightText: 2026 vibecut contributors
    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/
#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>

struct VibeCutMediaDocument {
    QString id;
    QString kind;
    QString text;
    int startFrame = -1;
    int endFrame = -1;
    QJsonObject metadata;

    QJsonObject toJson() const;
};

struct VibeCutMediaSearchHit {
    VibeCutMediaDocument document;
    int score = 0;
    QJsonObject toJson() const;
};

/** Provider-neutral project knowledge index.
 * Today it indexes transcript/subtitle text and clip names. Future scene,
 * OCR, face/subject, audio-event, and embedding extractors add documents
 * without changing the search/tool contract.
 */
class VibeCutMediaIndex
{
public:
    void clear();
    void add(const VibeCutMediaDocument &document);
    int size() const;
    QList<VibeCutMediaSearchHit> search(const QString &query, int limit = 25) const;
    bool rebuildFromCurrentProject(QString *error = nullptr);

private:
    QList<VibeCutMediaDocument> m_documents;
};
