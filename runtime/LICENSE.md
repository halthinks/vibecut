# Runtime licensing boundary

Copyright (c) 2026 halthinks. All rights reserved except where an individual file states otherwise.

This file defines the intended licensing boundary for the editor-agnostic runtime described by `EXTRACT_AND_LICENSE.md`.

## 1. Existing GPL editor code is not relicensed

Nothing in this file changes the license of Kdenlive, original VibeCut, or any existing file in `src/vibecut/` carrying:

`SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL`

Those files remain under their existing licenses and attribution requirements.

This repository as a combined Kdenlive/VibeCut editor fork is not converted into a proprietary editor by this notice.

## 2. Public protocol/schema artifacts

Unless an individual file says otherwise, files under `runtime/schema/` and `runtime/protocol.md` are intended to be published under the Apache License 2.0 so independent adapters can implement the protocol.

Their SPDX identifier is:

`Apache-2.0`

Use of a public schema or protocol does not grant rights to proprietary runtime implementation code.

## 3. Future extracted runtime implementation

A future editor-agnostic implementation under `runtime/src/` or a dedicated halthinks runtime repository may be distributed under a separate proprietary commercial license owned by halthinks, provided that implementation is a clean separable work and does not copy GPL implementation bodies from Kdenlive/original VibeCut.

Until a specific commercial license agreement is supplied with that implementation, no license is granted to copy, modify, distribute, sublicense, sell, or create derivative works from proprietary runtime implementation files except as required by applicable law.

A paid customer license is expected to define at least:

- licensed SKU (`Studio`, `OEM`, or support/update entitlement);
- permitted users/devices/hosts;
- protocol versions supported;
- update/support term;
- redistribution rights, if any;
- OEM embedding/service rights, if any;
- restrictions against representing Kdenlive or the GPL adapter as proprietary halthinks software;
- termination and warranty/liability terms.

Those commercial terms must be supplied by the signed/order-specific license agreement. This boundary notice is not itself a complete customer EULA.

## 4. GPL adapter remains separate and available under GPL

The Kdenlive-facing adapter remains GPL. It may speak the public runtime protocol over stdio, a local socket/named pipe, or another protocol-compatible transport.

A proprietary runtime must not be made proprietary merely by statically/in-process linking it into the GPL editor. The planned commercial seam is out-of-process protocol communication as documented in `runtime/protocol.md`.

## 5. Commercial SKU boundary

A commercial runtime delivery may include:

- an editor-agnostic runtime binary/service;
- runtime documentation;
- protocol compatibility guarantees;
- policy/provider/evidence packs owned or licensed for commercial distribution;
- support and updates.

It does not grant the customer a proprietary license to Kdenlive or the GPL adapter.

## 6. No trademark transfer

This file grants no rights to KDE/Kdenlive trademarks or third-party marks. Runtime and adapter branding must preserve project attribution and must not imply that Kdenlive itself is a proprietary halthinks NLE.

## 7. Extraction condition

There is no proprietary runtime merely because this directory or license file exists. The runtime is considered extracted only when the acceptance conditions in `EXTRACT_AND_LICENSE.md` are met, including editor-independent build/tests and a clean implementation boundary.

## 8. Legal review

This is an engineering/licensing boundary document, not legal advice. Before first commercial distribution, have qualified counsel review the implementation, dependency graph, SPDX headers, protocol boundary, packaging, and final customer license terms.
