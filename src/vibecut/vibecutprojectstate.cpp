/*
    SPDX-FileCopyrightText: 2026 vibecut contributors
    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#include "vibecuttools.h"

#include "core.h"
#include "doc/kdenlivedoc.h"
#include "vibecutprojectrevision.h"

quint64 VibeCutTools::projectRevision() const
{
    if (!m_revisionTracker) {
        m_revisionTracker = new VibeCutProjectRevisionTracker(const_cast<VibeCutTools *>(this));
    }

    if (!pCore || !pCore->currentDoc()) {
        m_revisionTracker->observe(nullptr);
        return m_revisionTracker->revision();
    }

    const auto stack = pCore->currentDoc()->commandStack();
    m_revisionTracker->observe(stack.get());
    return m_revisionTracker->revision();
}
