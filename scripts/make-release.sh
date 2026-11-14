#!/usr/bin/env bash
# Builds the source tarball of a tagged NekoControls version and puts it on a DRAFT
# GitHub release:
#
#     scripts/make-release.sh 0.2.0 [--notes "text"]
#
# The tag v<version> must exist and be pushed. The tarball is `git archive` of that tag
# (prefix NekoControls-<version>/), gzip'd without a timestamp, so it is reproducible.
# Projects pin THIS file, not GitHub's generated "Source code" archive:
#
#     URL      https://github.com/fhuber83/NekoControls/releases/download/v<version>/NekoControls-<version>.tar.gz
#     URL_HASH SHA256=<printed below>
#
# Releases are immutable once published (repository setting): check the draft, then
# publish it on GitHub or with `gh release edit v<version> --draft=false`. A mistake
# after that costs a version number.
set -euo pipefail

if [[ $# -lt 1 ]]; then
    echo "usage: $0 <version> [--notes \"text\"]" >&2
    exit 2
fi
version="$1"
shift
notes="NekoControls ${version}."
if [[ "${1:-}" == "--notes" ]]; then
    notes="$2"
fi

tag="v${version}"
name="NekoControls-${version}"
root="$(git -C "$(dirname "$0")" rev-parse --show-toplevel)"
cd "$root"

git rev-parse -q --verify "refs/tags/${tag}" >/dev/null || { echo "no tag ${tag}" >&2; exit 1; }
git ls-remote --exit-code --tags origin "refs/tags/${tag}" >/dev/null || { echo "${tag} is not pushed" >&2; exit 1; }

out="$(mktemp -d)"
tarball="${out}/${name}.tar.gz"
git archive --format=tar --prefix="${name}/" "${tag}" | gzip -n -9 > "${tarball}"
sha="$(sha256sum "${tarball}" | cut -d' ' -f1)"

gh release create "${tag}" "${tarball}" --draft --verify-tag --title "${name}" \
    --notes "${notes}

Pin with CMake FetchContent:

\`\`\`cmake
FetchContent_Declare(neko_controls
    URL      https://github.com/fhuber83/NekoControls/releases/download/${tag}/${name}.tar.gz
    URL_HASH SHA256=${sha})
\`\`\`"

echo "draft release ${tag}: ${tarball}"
echo "SHA256=${sha}"
