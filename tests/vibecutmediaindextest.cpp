/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "catch.hpp"
#include "vibecut/vibecutmediaindex.h"

TEST_CASE("media index ranks exact transcript phrases above token-only matches", "[vibecut][media]")
{
    VibeCutMediaIndex index;
    VibeCutMediaDocument a;
    a.id = QStringLiteral("a"); a.kind = QStringLiteral("transcript"); a.text = QStringLiteral("the red gearbox prototype failed"); a.startFrame = 20;
    VibeCutMediaDocument b;
    b.id = QStringLiteral("b"); b.kind = QStringLiteral("clip"); b.text = QStringLiteral("gearbox camera red"); b.startFrame = 10;
    index.add(a); index.add(b);
    const QList<VibeCutMediaSearchHit> hits = index.search(QStringLiteral("red gearbox"));
    REQUIRE(hits.size() == 2);
    CHECK(hits.first().document.id == QStringLiteral("a"));
    CHECK(hits.first().score > hits.last().score);
}
