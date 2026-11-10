#!/usr/bin/env bash
# scripts/release.sh — create a GitHub release with a complete source tarball.
#
# Usage:
#   ./scripts/release.sh 0.1.0
#
# Requirements: git, gh (authenticated), make_tarball.sh
set -euo pipefail

VERSION="${1:?Usage: $0 VERSION (e.g. 0.1.0)}"
TAG="v${VERSION}"
ROOT=$(cd "$(dirname "$0")/.." && pwd)

cd "$ROOT"

echo "==> Creating tag $TAG"
git tag -a "$TAG" -m "cui-http $VERSION" 2>/dev/null || echo "tag already exists, continuing"
git push origin "$TAG"

echo "==> Building tarball"
"$ROOT/scripts/make_tarball.sh" "$VERSION"
TARBALL="/tmp/cui-http-${VERSION}.tar.gz"
SHA=$(sha256sum "$TARBALL" | cut -d' ' -f1)

echo "==> Updating PKGBUILD sha256sums"
sed -i "s/^sha256sums=.*/sha256sums=('$SHA')/" "$ROOT/packaging/arch/PKGBUILD"

echo "==> Creating GitHub release"
gh release create "$TAG" \
    --title "cui-http $VERSION" \
    --notes "Native API client + CLI. Source tarball includes submodules." \
    "$TARBALL"

echo
echo "Released $TAG"
echo "Tarball: $TARBALL"
echo "SHA256:  $SHA"
echo
echo "Next: submit to AUR (cd aur-cui-http && cp $ROOT/packaging/arch/PKGBUILD . && makepkg --printsrcinfo > .SRCINFO && git add -A && git commit -m 'Update to $VERSION' && git push)"