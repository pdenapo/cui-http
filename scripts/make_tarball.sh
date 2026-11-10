#!/usr/bin/env bash
# scripts/make_tarball.sh — build a complete source tarball that INCLUDES the
# git submodules (vendor/CUI, vendor/d2sqlite3), which GitHub's auto-generated
# archive does NOT contain.
#
# Usage:
#   ./scripts/make_tarball.sh [VERSION]
# Example:
#   ./scripts/make_tarball.sh 0.1.0
#
# Output: /tmp/cui-http-<VERSION>.tar.gz
# The sha256 is printed at the end — paste it into packaging/arch/PKGBUILD.
set -euo pipefail

VERSION="${1:-}"
if [ -z "$VERSION" ]; then
    VERSION=$(git describe --tags --abbrev=0 2>/dev/null | sed 's/^v//' || echo "0.1.0")
fi

ROOT=$(cd "$(dirname "$0")/.." && pwd)
STAGE=$(mktemp -d)
NAME="cui-http-${VERSION}"
DEST="${STAGE}/${NAME}"
OUT="/tmp/${NAME}.tar.gz"

echo "==> Staging $NAME into $DEST"
mkdir -p "$DEST"

# Copy the tracked working tree (respects .gitignore).
git -C "$ROOT" archive --format=tar HEAD | tar -x -C "$DEST"

# Materialize submodules (they are empty dirs in the archive).
if [ -f "$ROOT/.gitmodules" ]; then
    # Try git submodule checkout first (needs network + git).
    (cd "$ROOT" && git submodule update --init --recursive) 2>/dev/null || true
fi
for sm in $(git -C "$ROOT" config --file "$ROOT/.gitmodules" --get-regexp path 2>/dev/null | awk '{print $2}'); do
    if [ -d "$ROOT/$sm/.git" ] || [ -f "$ROOT/$sm/.git" ]; then
        mkdir -p "$DEST/$(dirname "$sm")"
        # Copy the submodule working tree, excluding its .git.
        rsync -a --exclude='.git' "$ROOT/$sm/" "$DEST/$sm/" 2>/dev/null \
            || cp -a "$ROOT/$sm/." "$DEST/$sm/"
    fi
done

# Remove build artifacts / caches that may have been copied.
rm -rf "$DEST"/build* "$DEST"/.dub "$DEST"/*.db "$DEST"/requests.db

echo "==> Creating $OUT"
tar -czf "$OUT" -C "$STAGE" "$NAME"

# Cleanup
rm -rf "$STAGE"

echo
echo "Created: $OUT"
echo "Size:    $(du -h "$OUT" | cut -f1)"
echo "SHA256:  $(sha256sum "$OUT" | cut -d' ' -f1)"
echo
echo "Next steps:"
echo "  1. Upload $OUT to the GitHub release (gh release upload v$VERSION $OUT)"
echo "  2. Put the SHA256 above into packaging/arch/PKGBUILD (sha256sums)"
echo "  3. Regenerate .SRCINFO: makepkg --printsrcinfo > .SRCINFO"