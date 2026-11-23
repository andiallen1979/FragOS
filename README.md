# FragOS Gentoo Portage Overlay

Gentoo overlay for FragOS system utilities and **`frag-ai`**, a local AI Studio built around Ollama. Code Studio provides one natural-language workspace for chat, code, attached-image inspection, and image, audio, video, and research generation. Generated code and media have download links in the conversation.

## Repository Layout

```text
FragOS/
├── app-misc/
│   ├── frag-ai/
│   │   ├── Manifest
│   │   ├── frag-ai-1.0.0.ebuild
│   │   ├── frag-ai-1.1.0.ebuild
│   │   ├── frag-ai-1.2.0.ebuild
│   │   ├── frag-ai-1.3.3.ebuild
│   │   └── files/
│   │       ├── frag-ai-daemon
│   │       ├── frag-media-worker.py
│   │       └── studio/
│   └── fragOS-utils/
│       ├── fragOS-utils-9999.ebuild
│       └── metadata.xml
├── acct-group/
│   └── frag-ai/
│       ├── Manifest
│       └── frag-ai-0.ebuild
├── metadata/
│   └── layout.conf
├── profiles/
│   └── repo_name
└── src/
    ├── Makefile
    └── ps1cfg.c
```

`app-misc/frag-ai` uses versioned ebuilds. The current unified Studio implementation is packaged as `frag-ai-1.3.3`; its Python ebuild follows Gentoo's selected Python target. Shared application assets and daemon code live in its `files/` directory.

## Publish changes to `https://github.com/andiallen1979/FragOS`

Commit and push from the repository checkout:
   ```bash
   git add .
   git commit -m "Update FragOS overlay packages"
   git push -u origin main
   ```

## Install via Gentoo Portage (`eselect repository` / `/etc/portage/repos.conf`)

1. Register the overlay in `/etc/portage/repos.conf/fragos.conf`:
   ```ini
   [FragOS]
   location = /var/db/repos/FragOS
   sync-type = git
   sync-uri = https://github.com/andiallen1979/FragOS.git
   auto-sync = yes
   ```
2. Sync and accept the testing keyword for `frag-ai`:
   ```bash
   emaint sync -r FragOS
   mkdir -p /etc/portage/package.accept_keywords
   echo "app-misc/frag-ai **" >> /etc/portage/package.accept_keywords/frag-ai
   ```
3. Install Frag-AI and its account group:
   ```bash
   emerge -av app-misc/frag-ai
   ```
   Then add your user to `frag-ai`, start Ollama, run `emerge --config app-misc/frag-ai` to pull the default models, and enable `frag-ai-studio.service` for your user. To enable image/audio/video jobs, run `frag-ai-media-setup` once in that user account. Enable Portage's `cuda` USE flag on supported systems for PyTorch GPU acceleration.
