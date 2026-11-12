#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 TJ Griffiths <teej.d.man@gmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Builds krdc-ai for Windows x86_64 from inside an MSYS2 UCRT64 environment
# and assembles a portable zip: bin/krdc.exe with the Qt/KF6/FreeRDP runtime,
# the protocol plugins, and the breeze icon theme.
#
# The source tree is assembled exactly like pkg/PKGBUILD does for the AUR
# package: upstream KRDC at the pinned commit + pkg/krdc-ai.patch. So the
# Windows artifact carries the same patch series as the Linux package.
#
# Windows is deliberately RDP-only (VNC and SPICE off): SPICE needs
# spice-glib/GTK (unavailable), and vncclientthread.cpp / sshtunnelthread.cpp
# use raw POSIX sockets (poll.h, sys/socket.h) that MinGW does not provide.
# The AI panel and the dark mode filter are RDP-backend features, so the
# portable build loses nothing.
#
# Locally on Windows: install MSYS2 (https://www.msys2.org), open the
# "MSYS2 UCRT64" shell, then:
#   ./winbuild/build-windows.sh           # build + package
#   ./winbuild/build-windows.sh --test    # also run the AI engine test
#
# This is also the script .github/workflows/windows.yml runs.
#
# Output: $KRDC_WIN_WORK/dist/krdc-ai-<pkgver>-<pkgrel>-win64.zip
set -euo pipefail

[[ ${MSYSTEM:-} == UCRT64 ]] || { echo "Run this from an MSYS2 UCRT64 shell" >&2; exit 1; }

repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
work=$(cygpath -u "${KRDC_WIN_WORK:-$repo/../krdc-win-build}")
run_test=0
[[ "${1:-}" == --test ]] && run_test=1

# Mirror the AUR package's pinned source so both artifacts are the same code.
pkgbuild=$repo/pkg/PKGBUILD
commit=$(sed -n 's/^_commit=//p' "$pkgbuild")
pkgver=$(sed -n 's/^pkgver=//p' "$pkgbuild")
pkgrel=$(sed -n 's/^pkgrel=//p' "$pkgbuild")

# --- dependencies -------------------------------------------------------------
# Single source of truth: CI preinstalls nothing beyond base + git, this
# check installs whatever is missing (a no-op when everything is present).
mingw_deps=(
  gcc cmake ninja pkgconf extra-cmake-modules qt6-base qtkeychain
  breeze-icons freerdp libvncserver
  kbookmarks kcmutils kcompletion kconfig kcrash kdnssd kguiaddons
  ki18n kiconthemes kio knotifyconfig knotifications solid
  kstatusnotifieritem kwidgetsaddons kwindowsystem kxmlgui
)
msys_deps=(git patch tar zip gettext python)
missing=()
for p in "${msys_deps[@]}"; do
  pacman -Qq "$p" >/dev/null 2>&1 || missing+=("$p")
done
for p in "${mingw_deps[@]}"; do
  pacman -Qq "mingw-w64-ucrt-x86_64-$p" >/dev/null 2>&1 || missing+=("mingw-w64-ucrt-x86_64-$p")
done
if ((${#missing[@]})); then
  # MSYS2 mirrors occasionally stall mid-download and abort the whole
  # pacman transaction; retry a few times before giving up.
  for attempt in 1 2 3; do
    pacman -S --needed --noconfirm --disable-download-timeout "${missing[@]}" && break
    if ((attempt == 3)); then exit 1; fi
    echo "pacman attempt $attempt failed; retrying in 10s" >&2
    sleep 10
  done
fi

# --- source ---------------------------------------------------------------------
src=$work/src/krdc
mkdir -p "$src"
(
  cd "$src"
  if [[ ! -d .git ]]; then
    git init -q
    git remote add origin https://invent.kde.org/network/krdc.git
  fi
  # Same reset the PKGBUILD prepare() does, so the patch always applies clean.
  git checkout -f -- . 2>/dev/null || true
  git clean -ffdq
  # Shallow fetch of the pinned commit; fall back to a full fetch if the
  # server refuses fetch-by-sha (FETCH_HEAD would be the wrong tree there,
  # so check the pinned commit out by name instead).
  if git fetch -q --depth 1 origin "$commit" 2>/dev/null; then
    git checkout -q --detach FETCH_HEAD
  else
    git fetch -q origin
    git checkout -q --detach "$commit"
  fi
  patch -Np1 -i "$repo/pkg/krdc-ai.patch"
  # Windows-only fixes for pre-existing upstream bugs (see the patch header);
  # applied after krdc-ai.patch, and deliberately not part of the
  # AUR/upstream patch series.
  patch -Np1 -i "$repo/winbuild/windows-porting.patch"
)

# --- configure & build ------------------------------------------------------------
prefix=$work/root
build=$work/build
rm -rf "$prefix"
# KDE_INSTALL_PLUGINDIR must equal KDE_INSTALL_QTPLUGINDIR here: the exe
# carries KRDC_PLUGIN_INSTALL_DIR as "<exe dir>/../<QTPLUGINDIR>" and loads
# <that>/krdc at runtime, so plugins installed under the same relative dir
# are found in the portable layout without any env setup.
# LibSSH is off deliberately: MSYS2's FreeRDP drags it in, which would make
# core/sshtunnelthread.cpp part of the build, and that file is POSIX sockets
# (arpa/inet.h, poll.h) that MinGW cannot compile. SSH-tunnelled connections
# are an optional Linux feature; without it FindLibSSH fails softly and the
# file is not built at all.
cmake -S "$src" -B "$build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$(cygpath -w "$prefix")" \
  -DWITH_RDP=YES -DWITH_VNC=NO -DWITH_SPICE=NO -DWITH_AI=YES \
  -DCMAKE_DISABLE_FIND_PACKAGE_LibSSH=YES \
  -DKDE_INSTALL_QTPLUGINDIR=plugins -DKDE_INSTALL_PLUGINDIR=plugins
cmake --build "$build"
cmake --install "$build"

# --- AI engine test (advisory) -------------------------------------------------------
# run_ai_test.sh reconfigures its own build dir with default options and only
# builds the test target, so the absent VNC *plugin* sources never compile;
# libvncserver is installed above purely so its configure finds libvncclient.
# Advisory for now: the mock-server harness under Windows is the new part
# here, not the code under test, and a green artifact should not hinge on it.
if ((run_test)); then
  # run_ai_test.sh reconfigures its own build dir with default options and
  # only builds the test target, so the absent VNC *plugin* sources never
  # compile; libvncserver is installed above purely so its configure finds
  # libvncclient. Pre-seed the cache with the LibSSH disable (cmake keeps
  # cached values that the script's own command line does not set) so
  # sshtunnelthread.cpp is skipped there too.
  # Advisory for now: the mock-server harness under Windows is the new part
  # here, not the code under test, and a green artifact should not hinge on it.
  cmake -S "$src" -B "$work/build-test" -G Ninja \
    -DCMAKE_DISABLE_FIND_PACKAGE_LibSSH=YES >/dev/null
  if ! "$src/test/run_ai_test.sh" "$work/build-test"; then
    echo "::warning::AI engine test failed on Windows (advisory; artifact still produced)"
  fi
fi

# --- portable layout --------------------------------------------------------------
distname=krdc-ai-$pkgver-$pkgrel-win64
dist=$work/dist/$distname
dbin=$dist/bin
rm -rf "$dist"
mkdir -p "$dbin"
cp "$prefix/bin/krdc.exe" "$dbin/"
for f in "$prefix"/bin/libkrdccore*.dll; do cp "$f" "$dbin/"; done
cp -r "$prefix/plugins" "$dist/plugins"

# FreeRDP 3 loads its channel plugins dynamically at runtime; keep MSYS2's
# lib/freerdp3 next to bin/ so winpr's module search (relative to the
# executable) finds them in the portable tree.
mkdir -p "$dist/lib"
cp -r "$MINGW_PREFIX/lib/freerdp3" "$dist/lib/freerdp3"
# cmake/MSYS2 leave MinGW import libs (.dll.a) beside the plugins; the runtime
# needs only the .dll, so drop the link-time stubs from the portable tree.
find "$dist" -name '*.dll.a' -delete

# Qt runtime: platform/style/imageformat plugins, Qt dlls, mingw runtimes.
"$MINGW_PREFIX/bin/windeployqt.exe" --release --compiler-runtime \
  --no-translations "$dbin/krdc.exe" >/dev/null

# Breeze icons on Windows ship as binary Qt resources, not an SVG tree
# (MSYS2/Craft distribute them this way). KF6 apps pick them up from a
# data/ directory next to the executable. Copy only the icon resources; the
# package's bin/data also holds unrelated data from other KF6 packages.
mkdir -p "$dbin/data/icons"
cp -r "$MINGW_PREFIX/bin/data/icons/breeze" "$dbin/data/icons/breeze"
cp "$MINGW_PREFIX/bin/data/icontheme.rcc" "$dbin/data/"

# Copy every MSYS2 dll the binaries import (KF6, FreeRDP, winpr, qtkeychain,
# openssl, ...) into bin/, recursively. Anything outside $MINGW_PREFIX/bin is
# a Windows system dll and is left alone.
copy_deps() {
  local -a queue=("$@")
  local -A seen=()
  local f dep found
  while ((${#queue[@]})); do
    f=${queue[0]}
    queue=("${queue[@]:1}")
    while read -r dep; do
      [[ -e $dbin/$dep ]] && continue
      [[ -n ${seen[${dep,,}]:-} ]] && continue
      seen[${dep,,}]=1
      found=$(find "$MINGW_PREFIX/bin" -maxdepth 1 -iname "$dep" -print -quit 2>/dev/null)
      if [[ -n $found ]]; then
        cp "$found" "$dbin/$dep"
        queue+=("$dbin/$dep")
      fi
    done < <(objdump -p "$f" 2>/dev/null | awk 'tolower($1)=="dll" && tolower($2) ~ /^name:?$/{print $3}')
  done
}
copy_deps "$dbin/krdc.exe" \
  "$dbin"/*.dll \
  "$dist"/plugins/krdc/*.dll \
  "$dist"/plugins/krdc/kcms/*.dll \
  "$dist"/lib/freerdp3/*.dll

cat >"$dist/README.txt" <<EOF
krdc-ai $pkgver (pkgrel $pkgrel), Windows x86_64, RDP-only build

Run bin/krdc.exe. Nothing to install.

* Dark mode filter: Ctrl+D (or the Dark Mode Filter entry in the menu)
  while connected. Recolours light remote windows to a dark scheme and
  leaves photos and video alone. Remembered across sessions.
* AI assistant: enable the dock in the menu; any OpenAI-compatible
  endpoint (Ollama, llama.cpp, openai.com, ...). API keys are stored in
  the Windows credential store.
* VNC and SPICE are not in this build.
EOF

( cd "$work/dist" && zip -qr "$distname.zip" "$distname" )
echo "Artifact: $work/dist/$distname.zip"
