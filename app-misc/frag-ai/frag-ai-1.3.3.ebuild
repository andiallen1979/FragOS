# Copyright 2026 FragOS Authors
# Distributed under the terms of the GNU General Public License v2

EAPI=8
PYTHON_COMPAT=( python3_12 python3_13 python3_14 )
inherit desktop systemd xdg python-single-r1

DESCRIPTION="Frag-ai unified local AI Studio for code, vision, and media workflows"
HOMEPAGE="https://github.com/andiallen1979/FragOS"
LICENSE="MIT"
SLOT="0"
KEYWORDS="~amd64 ~arm64"
IUSE="cuda"

BDEPEND="acct-group/frag-ai"
RDEPEND="
	acct-group/frag-ai
	${PYTHON_DEPS}
	dev-python/soundfile
	media-video/ffmpeg
	sci-ml/accelerate[${PYTHON_SINGLE_USEDEP}]
	!cuda? ( sci-ml/pytorch[${PYTHON_SINGLE_USEDEP},-cuda,-rocm] )
	cuda? ( sci-ml/pytorch[${PYTHON_SINGLE_USEDEP},cuda,-rocm] )
	$(python_gen_cond_dep 'sci-ml/safetensors[${PYTHON_USEDEP}]')
	sci-ml/transformers[${PYTHON_SINGLE_USEDEP}]
	!cuda? ( sci-ml/torchvision[${PYTHON_SINGLE_USEDEP},-cuda,-rocm] )
	cuda? ( sci-ml/torchvision[${PYTHON_SINGLE_USEDEP},cuda,-rocm] )
	dev-cpp/glog[llvm-libunwind]
	sci-ml/ollama
"

S="${WORKDIR}"

pkg_setup() {
	python-single-r1_pkg_setup
}

src_install() {
	insinto /usr/share/frag-ai/studio
	doins -r "${FILESDIR}/studio/."

	exeinto /usr/libexec
	doexe "${FILESDIR}/frag-ai-daemon"
	doexe "${FILESDIR}/frag-media-worker.py"
	python_fix_shebang "${ED}/usr/libexec/frag-ai-daemon" "${ED}/usr/libexec/frag-media-worker.py"

	dobin "${FILESDIR}/code-studio"
	dobin "${FILESDIR}/frag-ai-setup"
	dobin "${FILESDIR}/frag-ai-media-setup"
	python_fix_shebang "${ED}/usr/bin/frag-ai-media-setup"
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
	elog "5. Set up image/audio/video support once per user: frag-ai-media-setup"
	elog "6. Enable the cuda USE flag for GPU acceleration where supported."
	elog "7. Use Code Studio for chat, code, image inspection, and media generation from one prompt."
}
