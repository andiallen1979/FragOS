# FragOS Portage Overlay & `fragOS-utils`

Zero-dependency system utilities for **FragOS** / **Gentoo Linux**, featuring **`ps1cfg`** (POSIX `termios` Bash prompt customizer with GPM console mouse support, `/proc/net` telemetry, and automatic User/Root EUID status block).

## Repository Layout

```text
FragOS/
├── app-misc/
│   └── fragOS-utils/
│       ├── fragOS-utils-9999.ebuild
│       └── metadata.xml
├── metadata/
│   └── layout.conf
├── profiles/
│   └── repo_name
└── src/
    ├── Makefile
    └── ps1cfg.c
```

## Push to `https://github.com/andiallen1979/FragOS`

1. Extract this archive into your local `FragOS` git checkout:
   ```bash
   unzip fragOS-utils-gentoo-overlay.zip -d ~/FragOS
   cd ~/FragOS
   ```
2. Commit and push to GitHub:
   ```bash
   git add .
   git commit -m "Add app-misc/fragOS-utils ebuild and ps1cfg v1.6.0 source"
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
2. Sync and unmask the live `9999` ebuild:
   ```bash
   emaint sync -r FragOS
   mkdir -p /etc/portage/package.accept_keywords
   echo "app-misc/fragOS-utils **" >> /etc/portage/package.accept_keywords/fragOS-utils
   ```
3. Emerge `fragOS-utils`:
   ```bash
   emerge -av app-misc/fragOS-utils
   ```
