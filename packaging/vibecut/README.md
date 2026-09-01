# halthinks/VibeCut packaging layer

This directory contains packaging owned by the **halthinks/VibeCut** fork.

It is intentionally separate from the packaging inherited from Kdenlive and the original VibeCut repository. The inherited Flatpak, Snap, Linux, and Windows packaging remains available and should not be rewritten merely to identify this fork.

## Debian installer package

`build-deb.sh` builds a `.deb` package named:

```text
vibecut-halthinks_<version>_<architecture>.deb
```

The package is designed to coexist with a distribution Kdenlive installation:

- application files are installed under `/opt/vibecut-halthinks`;
- `/usr/bin/vibecut-halthinks` is a dedicated launcher;
- `/usr/share/applications/org.halthinks.vibecut.desktop` is a dedicated desktop entry;
- the package does **not** declare itself to be the distribution `kdenlive` package;
- Kdenlive/VibeCut licensing, resources, and application internals remain intact inside the staged install.

## Build the package

From the repository root on a Debian build host with the VibeCut build dependencies installed:

```bash
bash packaging/vibecut/build-deb.sh
```

By default the script:

1. validates the VibeCut dependency contract;
2. configures a Release build with `CMAKE_INSTALL_PREFIX=/opt/vibecut-halthinks`;
3. compiles the tree;
4. runs the `vibecut*` CTest suite;
5. stages `cmake --install` into a Debian package root;
6. derives runtime shared-library dependencies with `dpkg-shlibdeps` when available;
7. creates the `.deb` with `dpkg-deb`.

The resulting installer is written to:

```text
packages/
```

### Useful overrides

```bash
VIBECUT_PACKAGE_VERSION=0.1.0 \
VIBECUT_PACKAGE_BUILD_DIR=/tmp/vibecut-package-build \
VIBECUT_PACKAGE_OUTPUT_DIR=$PWD/packages \
VIBECUT_JOBS=8 \
bash packaging/vibecut/build-deb.sh
```

Set `VIBECUT_PACKAGE_SKIP_TESTS=1` only for packaging diagnostics. Release packages should be produced only after the verification and smoke-test gates are green.

## Install locally

```bash
sudo apt install ./packages/vibecut-halthinks_<version>_<architecture>.deb
```

Launch with:

```bash
vibecut-halthinks
```

or the **VibeCut (halthinks)** desktop entry.

## Removal

```bash
sudo apt remove vibecut-halthinks
```

Because the fork is installed in its own prefix, removal does not remove the system Kdenlive package.

## Release rule

Creating a package file is not by itself a release qualification. A distributable halthinks/VibeCut package should require:

- dependency validation;
- successful configure and compile;
- successful `vibecut*` tests;
- inspect → plan → approve → edit → verify → Undo smoke testing;
- render and cancellation smoke testing;
- Whisper/subtitle smoke testing where the runtime dependencies are present;
- Review/Auto/Turbo policy testing.
