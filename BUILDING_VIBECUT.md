# Building VibeCut locally

VibeCut is built inside the Kdenlive source tree and follows the dependency minimums of the current development branch.

## Current minimum build stack

The repository currently declares:

- Qt **6.10.0+**
- KDE Frameworks / ECM **6.21.0+**
- MLT / MLT++ **7.38.0+**
- KDDockWidgets Qt6 **2.4.0+**
- FFmpeg development APIs: `avformat`, `avcodec`, `swresample`, `avutil`
- OpenTimelineIO
- Imath
- CMake, a C++ compiler, pkg-config, and Ninja

Debian 13 (`trixie`) stable is not sufficient for this development tree: its Qt and MLT packages are below the declared minimums. The repository therefore provides a Debian bootstrap that installs the matching development stack explicitly from Debian `sid`, while pinning sid at low priority for unrelated future installs.

## One-command Debian bootstrap + build

From the repository root:

```bash
bash scripts/vibecut-bootstrap-debian.sh
```

That script:

1. detects Debian and root/sudo access;
2. adds a pinned Debian `sid` source when needed;
3. installs the complete Qt6/KF6/MLT/FFmpeg/KDDockWidgets/OTIO/Imath build dependency set;
4. runs `scripts/vibecut-build-env-check.sh`;
5. runs `scripts/vibecut-verify.sh`.

To install dependencies and stop before compilation:

```bash
VIBECUT_BOOTSTRAP_SKIP_VERIFY=1 bash scripts/vibecut-bootstrap-debian.sh
```

## Verification only

Once dependencies are present:

```bash
bash scripts/vibecut-verify.sh
```

The verification gate performs:

1. authoritative dependency/component/version checks;
2. CMake configure with `BUILD_TESTING=ON`;
3. native build;
4. all repository-local tests matching `^vibecut`.

Useful overrides:

```bash
VIBECUT_BUILD_DIR=/path/to/build \
VIBECUT_BUILD_TYPE=Debug \
VIBECUT_JOBS=8 \
bash scripts/vibecut-verify.sh
```

## No GitHub Actions dependency

The VibeCut verification path is intentionally repository-local. It does **not** require or invoke GitHub Actions or any hosted CI workflow.

## Release gate

Do not merge `agent/vibecut-architecture-slices` into the default `vibecut` branch solely because static source/API audits look correct. The minimum release evidence remains:

- successful `scripts/vibecut-verify.sh` run;
- hands-on inspect → plan → approve → edit → verify → Undo smoke tests;
- long-job Whisper/render/cancel smoke tests;
- Review/Auto/Turbo and `.vibecutpolicy.json` policy smoke tests.
