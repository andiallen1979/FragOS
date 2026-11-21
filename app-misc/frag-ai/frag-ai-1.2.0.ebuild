# Copyright 2026 FragOS Authors
# Distributed under the terms of the GNU General Public License v2

EAPI=8
inherit desktop systemd xdg

DESCRIPTION="Frag-ai local IDE (Code Studio) and Ollama coding stack"
HOMEPAGE="https://github.com/andiallen1979/FragOS"
LICENSE="MIT"
SLOT="0"
KEYWORDS="~amd64 ~arm64"

BDEPEND="acct-group/frag-ai"
RDEPEND="
	acct-group/frag-ai
	dev-lang/python:3.12
	sci-ml/ollama
"

S="${WORKDIR}"

src_install() {
	insinto /usr/share/frag-ai/studio
	doins -r "${FILESDIR}/studio/."

	exeinto /usr/libexec
	doexe "${FILESDIR}/frag-ai-daemon"
	doexe "${FILESDIR}/frag-media-worker.py"

	dobin "${FILESDIR}/code-studio"
	dobin "${FILESDIR}/frag-ai-setup"
	dobin "${FILESDIR}/frag-mm"

	systemd_douserunit "${FILESDIR}/frag-ai-studio.service"
	doicon -s scalable "${FILESDIR}/code-studio.svg"
	make_desktop_entry "code-studio" "Code Studio" "code-studio" "Development;IDE;"
	make_desktop_entry "frag-mm" "Frag-MM Model Manager" "code-studio" "Development;Utility;"
}

pkg_config() {
	einfo "Running Frag-ai model setup..."
	/usr/bin/frag-ai-setup
}

pkg_postinst() {
	xdg_pkg_postinst
	elog "1. Add users to the frag-ai group:  usermod -aG frag-ai <user>"
	elog "2. Start Ollama:                    systemctl enable --now ollama.service"
	elog "3. Pull models:                     emerge --config app-misc/frag-ai"
	elog "4. Enable per-user Studio backend:  systemctl --user enable --now frag-ai-studio.service"
}
