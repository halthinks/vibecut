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
 * Today it indexes transcript/subtitle text, clip names and persistent media
 * evidence. Semantic embedding refresh consumes the same document snapshot so
 * lexical and semantic retrieval cannot silently disagree about what the
 * current project index contains.
 */
class VibeCutMediaIndex
{
public:
    void clear();
    void add(const VibeCutMediaDocument &document);
    int size() const;
    QList<VibeCutMediaDocument> documents() const;
    QList<VibeCutMediaSearchHit> search(const QString &query, int limit = 25) const;
    bool rebuildFromCurrentProject(QString *error = nullptr);

private:
    QList<VibeCutMediaDocument> m_documents;
};
