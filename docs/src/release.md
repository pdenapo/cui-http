# Release & Packaging

See [RELEASE.md](https://github.com/yappologistic/cui-http/blob/main/RELEASE.md)
for the full process. Summary:

- Build tarball with submodules: `./scripts/make_tarball.sh VERSION`
- Publish: `./scripts/release.sh VERSION` (tags, uploads tarball, creates release)
- AUR: copy `packaging/arch/PKGBUILD`, run `makepkg --printsrcinfo > .SRCINFO`, push
- Debian: `cp -r packaging/debian . && dpkg-buildpackage -us -uc -b`
- GitHub Actions: `.github/workflows/build.yml` (multi-OS matrix) and optional
  release job on tags.