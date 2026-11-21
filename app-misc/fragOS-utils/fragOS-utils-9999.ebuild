# Copyright 2026 Gentoo Authors
# Distributed under the terms of the GNU General Public License v2

EAPI=8

inherit toolchain-funcs

DESCRIPTION="FragOS core system utilities including ps1cfg POSIX termios prompt customizer"
HOMEPAGE="https://github.com/andiallen1979/FragOS"

if [[ ${PV} == *9999* ]]; then
	inherit git-r3
	EGIT_REPO_URI="https://github.com/andiallen1979/FragOS.git"
else
	SRC_URI="https://github.com/andiallen1979/FragOS/archive/refs/tags/v${PV}.tar.gz -> ${P}.tar.gz"
	KEYWORDS="~amd64 ~arm64 ~x86"
fi

LICENSE="MIT"
SLOT="0"
IUSE=""

src_compile() {
	if [[ -d src ]]; then
		cd src || die
	fi
	emake CC="$(tc-getCC)"
}

src_install() {
	if [[ -d src ]]; then
		cd src || die
	fi
	emake DESTDIR="${D}" PREFIX="${EPREFIX}/usr" install
}
