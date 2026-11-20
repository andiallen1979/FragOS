/*
 * ps1cfg.c — Zero-Dependency POSIX termios Bash Prompt Customizer
 * Part of fragOS-utils (https://github.com/andiallen1979/FragOS)
 *
 * Features:
 *  - Pure POSIX <termios.h>, <unistd.h>, <signal.h>, <sys/select.h>, <sys/socket.h>
 *  - Works in pure terminal / headless TTY environments (no X11 or Wayland required)
 *  - Full standard keyboard navigation: Arrow keys, Tab/Shift+Tab, Enter/Space, Esc
 *  - Optional Linux Console GPM mouse (--gpm) via /dev/gpmctl (zero libgpm dependency)
 *    and live [M] toggle inside the TUI (GPM on raw TTY, XTerm SGR in terminal emulators)
 *  - Clean CLI flags: -h/--help, --help-html [FILE], -v/--version, --gpm, --tty
 *  - Automatically manages current user (~/.custom_prompt.sh + ~/.bashrc hook)
 *    or root (/etc/profile.d/99-custom-prompt.sh) when launched with sudo/root
 *  - Embeds # PS1CFG_STATE header to reload your exact settings on startup
 *  - Graphics-editor painted color square selector + Brush routine (FG/BG)
 *  - One-key [U] User/Root Status Square toggle (green user / red root)
 *  - Per-segment visibility toggles (enable/disable any segment)
 *  - Powerline, Rounded, Slanted, Clean, and Pipe Pill (|$PWD:\h| |) styles
 *  - Nerd Font icons with automatic 7-bit ASCII fallback on Linux TTY (--tty)
 *  - Zero-fork /proc/net/tcp & /proc/net/route socket/gateway telemetry
 *  - Atomic temp-file write + .bak backup + clean uninstall ('X')
 */

#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <termios.h>
#include <unistd.h>

#define PS1CFG_VERSION "1.6.0"
#define TUI_BOX_WIDTH 74
#define GPM_NODE_CTL "/dev/gpmctl"
#define GPM_EVT_MOVE 0x0001
#define GPM_EVT_DOWN 0x0004
#define GPM_BTN_LEFT 0x04

typedef struct {
    unsigned short eventMask;
    unsigned short defaultMask;
    unsigned short minMod;
    unsigned short maxMod;
    int pid;
    int vc;
} GpmConnectRaw;

typedef struct {
    unsigned char buttons;
    unsigned char modifiers;
    unsigned short vc;
    short dx, dy, x, y;
    int type;
    int clicks;
    int margin;
    short wdx, wdy;
} GpmEventRaw;

typedef struct {
    const char *name;
    uint8_t r, g, b;
} ColorSquare;

static const ColorSquare PALETTE[] = {
    {"Crust",     0x11, 0x11, 0x1b}, {"Mantle",   0x18, 0x18, 0x25},
    {"Base",      0x1e, 0x1e, 0x2e}, {"Surface0", 0x31, 0x32, 0x44},
    {"Surface2",  0x58, 0x5b, 0x70}, {"Subtext",  0xa6, 0xad, 0xc8},
    {"Text",      0xcd, 0xd6, 0xf4}, {"Lavender", 0xb4, 0xbe, 0xfe},
    {"Blue",      0x89, 0xb4, 0xfa}, {"Sapphire", 0x74, 0xc7, 0xec},
    {"Sky",       0x89, 0xdc, 0xeb}, {"Teal",     0x94, 0xe2, 0xd5},
    {"Green",     0xa6, 0xe3, 0xa1}, {"Yellow",   0xf9, 0xe2, 0xaf},
    {"Peach",     0xfa, 0xb3, 0x87}, {"Maroon",   0xeb, 0xa0, 0xac},
    {"Red",       0xf3, 0x8b, 0xa8}, {"Mauve",    0xcb, 0xa6, 0xf7},
    {"Pink",      0xf5, 0xc2, 0xe7}, {"Flamingo", 0xf2, 0xcd, 0xcd},
    {"PureWhite", 0xff, 0xff, 0xff}, {"Amber",    0xff, 0xbf, 0x00},
    {"Emerald",   0x10, 0xb9, 0x81}, {"Crimson",  0xdc, 0x26, 0x26}
};
#define PALETTE_COUNT ((int)(sizeof(PALETTE) / sizeof(PALETTE[0])))

typedef struct {
    uint8_t r, g, b;
    bool transparent;
} RgbColor;

typedef struct {
    const char *name;
    const char *nerd_sep;
    const char *ascii_sep;
} PromptStyle;

static const PromptStyle STYLES[] = {
    {"Powerline", "\xee\x82\xb0", ">"},
    {"Rounded",   "\xee\x82\xb4", ")"},
    {"Slanted",   "\xee\x82\xbc", "/"},
    {"Clean",     " ",           " "},
    {"Pipe Pill", "|",           "|"}
};
#define STYLE_COUNT ((int)(sizeof(STYLES) / sizeof(STYLES[0])))

typedef struct {
    const char *label;
    const char *nerd_glyph;
    const char *ascii_glyph;
} IconPreset;

static const IconPreset ICONS[] = {
    {"Linux",  "\xef\x8c\x83", "@"},
    {"Folder", "\xef\x81\xbc", "~"},
    {"Net",    "\xef\x9b\xbf", "NET"},
    {"Bolt",   "\xef\x83\xa7", "#"},
    {"Term",   "\xef\x92\x89", "$"},
    {"Shield", "\xef\x84\xb2", "!"}
};
#define ICON_COUNT ((int)(sizeof(ICONS) / sizeof(ICONS[0])))

typedef struct {
    RgbColor seg_fg[4];
    RgbColor seg_bg[4];
    int seg_icon[4];
    bool seg_enabled[4];
    int style_idx;
    bool ascii_fallback;
    bool proc_net_enabled;
    bool euid_square;
} ProfileConfig;

typedef struct {
    bool system_managed;
    bool no_gui_env;
    bool is_linux_console;
    bool force_tty_ascii;
    bool gpm_flag;
    bool mouse_enabled;
    bool sgr_mouse_active;
    int gpm_fd;
    int vc_num;
    char target_path[512];
    char bashrc_path[512];
    int active_seg;
    int selected_square;
    bool target_bg;
    RgbColor brush;
    ProfileConfig active_cfg;
    char status_msg[1280];
} AppState;

static struct termios g_orig_termios;
static bool g_raw_active = false;
static AppState g_state;

static void write_str(const char *s, size_t len) {
    ssize_t r = write(STDOUT_FILENO, s, len);
    (void)r;
}

static void print_help(const char *prog) {
    printf("\n========================================================================\n");
    printf("  ps1cfg v%s — Zero-Dependency POSIX termios Bash Prompt Customizer\n", PS1CFG_VERSION);
    printf("========================================================================\n\n");
    printf("USAGE:\n");
    printf("  %s [OPTIONS]\n", prog);
    printf("  sudo %s               Customize and install the root (#) prompt\n\n", prog);
    printf("OPTIONS:\n");
    printf("  -h, --help            Display this comprehensive help manual and exit.\n");
    printf("  --help-html [FILE]    Write a styled standalone HTML help manual.\n");
    printf("                        Defaults to ~/Desktop/ps1cfg_help.html.\n");
    printf("  -v, --version         Print version and build details and exit.\n");
    printf("  --gpm                 Enable Linux console GPM mouse (/dev/gpmctl).\n");
    printf("  --tty                 Force pure-terminal 7-bit ASCII glyph mode.\n\n");
    printf("KEYBINDINGS:\n");
    printf("  Arrows / H,L  Navigate the 12x2 color palette and paint active segment.\n");
    printf("  Tab / S-Tab   Cycle forward/backward through prompt segments (1-4).\n");
    printf("  Enter / Space Apply the currently loaded brush color to active segment.\n");
    printf("  [1] - [4]     Jump directly to segment 1-4.\n");
    printf("  [V]           Show / Hide the currently selected segment.\n");
    printf("  [F] / [B]     Switch brush target between Foreground (FG) & Background (BG).\n");
    printf("  [C]           Clear background (transparent) on the active segment.\n");
    printf("  [T]           Cycle style: Powerline, Rounded, Slanted, Clean, Pipe Pill.\n");
    printf("  [P]           Load |$PWD:\\h| | Pipe Pill preset (green user / red root).\n");
    printf("  [U]           Toggle trailing User/Root Status Square (green vs. red).\n");
    printf("  [I]           Cycle Nerd Font / ASCII icon for the active segment.\n");
    printf("  [A]           Toggle 7-bit ASCII fallback vs. Nerd Font glyphs.\n");
    printf("  [N]           Toggle zero-fork /proc/net/tcp & /proc/net/route telemetry.\n");
    printf("  [M]           Toggle Mouse support (GPM console / XTerm SGR) live.\n");
    printf("  [S]           Save & Apply atomically (creates .bak backup, chmod 0644).\n");
    printf("  [X]           Uninstall managed prompt script to restore default PS1.\n");
    printf("  [Q] / [Esc]   Quit cleanly and restore terminal state via termios.\n\n");
}

static void export_help_html(const char *custom_path) {
    char path[512];
    if (custom_path && custom_path[0] != '\0') {
        snprintf(path, sizeof(path), "%s", custom_path);
    } else {
        const char *home = getenv("HOME");
        if (!home || home[0] == '\0') home = ".";
        snprintf(path, sizeof(path), "%s/Desktop/ps1cfg_help.html", home);
    }
    FILE *fp = fopen(path, "w");
    if (!fp) {
        fprintf(stderr, "Error: could not write %s: %s\n", path, strerror(errno));
        exit(EXIT_FAILURE);
    }
    fprintf(fp,
        "<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"UTF-8\">"
        "<title>ps1cfg v%s — Help Manual</title>"
        "<style>body{margin:0;background:#11111b;color:#cdd6f4;font-family:system-ui,sans-serif;line-height:1.6;padding:32px 16px}"
        ".wrap{max-width:920px;margin:0 auto}h1{color:#89b4fa;margin:0 0 6px}h2{color:#cba6f7;border-bottom:1px solid #313244;padding-bottom:6px;margin-top:28px}"
        ".card{background:#181825;border:1px solid #313244;border-radius:14px;padding:20px;margin:16px 0}"
        "code,kbd{font-family:ui-monospace,monospace;background:#313244;color:#f9e2af;padding:2px 6px;border-radius:6px;font-size:.92em}"
        "</style></head><body><div class=\"wrap\">"
        "<h1>ps1cfg v%s</h1><p>Zero-Dependency POSIX <code>termios</code> Bash Prompt Customizer (fragOS-utils)</p>"
        "<div class=\"card\"><h2>Keybindings</h2><ul>"
        "<li><kbd>Arrows</kbd> / <kbd>Enter</kbd>: Select and paint colors from the 24-color palette.</li>"
        "<li><kbd>1</kbd>-<kbd>4</kbd> / <kbd>Tab</kbd> / <kbd>V</kbd>: Select and show/hide segments.</li>"
        "<li><kbd>U</kbd>: Toggle trailing User/Root Status Square (green user / red root).</li>"
        "<li><kbd>P</kbd>: Load Pipe Pill preset.</li>"
        "<li><kbd>S</kbd> / <kbd>X</kbd> / <kbd>Q</kbd>: Save atomically / Uninstall / Quit.</li>"
        "</ul></div></div></body></html>\n",
        PS1CFG_VERSION, PS1CFG_VERSION);
    fclose(fp);
    printf("Wrote styled HTML help guide to: %s\n", path);
}

static void detect_environment(void) {
    const char *disp = getenv("DISPLAY");
    const char *wdisp = getenv("WAYLAND_DISPLAY");
    const char *term = getenv("TERM");
    g_state.no_gui_env = ((!disp || disp[0] == '\0') && (!wdisp || wdisp[0] == '\0'));
    g_state.is_linux_console = (term && (strcmp(term, "linux") == 0 || strncmp(term, "vt", 2) == 0));
    g_state.vc_num = 0;
    const char *tname = ttyname(STDIN_FILENO);
    if (tname && strncmp(tname, "/dev/tty", 8) == 0 && isdigit((unsigned char)tname[8])) {
        g_state.vc_num = atoi(tname + 8);
        g_state.is_linux_console = true;
    }
}

static void gpm_close_raw(void) {
    if (g_state.gpm_fd >= 0) {
        close(g_state.gpm_fd);
        g_state.gpm_fd = -1;
    }
}

static bool gpm_open_raw(void) {
    gpm_close_raw();
    if (!g_state.mouse_enabled) return false;

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return false;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", GPM_NODE_CTL);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(fd);
        return false;
    }

    GpmConnectRaw conn;
    memset(&conn, 0, sizeof(conn));
    conn.eventMask = GPM_EVT_MOVE | GPM_EVT_DOWN;
    conn.defaultMask = GPM_EVT_MOVE;
    conn.minMod = 0;
    conn.maxMod = 0xFFFF;
    conn.pid = (int)getpid();
    conn.vc = g_state.vc_num;

    if (write(fd, &conn, sizeof(conn)) != (ssize_t)sizeof(conn)) {
        close(fd);
        return false;
    }
    g_state.gpm_fd = fd;
    return true;
}

static void sync_mouse_mode(void) {
    gpm_close_raw();
    if (g_state.sgr_mouse_active) {
        write_str("\033[?1006l\033[?1000l", 16);
        g_state.sgr_mouse_active = false;
    }
    if (!g_state.mouse_enabled) return;

    if ((g_state.no_gui_env || g_state.gpm_flag) && gpm_open_raw()) {
        return;
    }
    if (!g_state.is_linux_console && !g_state.no_gui_env) {
        write_str("\033[?1000h\033[?1006h", 16);
        g_state.sgr_mouse_active = true;
    }
}

static void resolve_managed_paths(void) {
    g_state.system_managed = (geteuid() == 0);
    if (g_state.system_managed) {
        snprintf(g_state.target_path, sizeof(g_state.target_path),
                 "/etc/profile.d/99-custom-prompt.sh");
        g_state.bashrc_path[0] = '\0';
    } else {
        const char *home = getenv("HOME");
        if (!home || home[0] == '\0') home = ".";
        snprintf(g_state.target_path, sizeof(g_state.target_path),
                 "%s/.custom_prompt.sh", home);
        snprintf(g_state.bashrc_path, sizeof(g_state.bashrc_path),
                 "%s/.bashrc", home);
    }
}

static void restore_terminal(void) {
    gpm_close_raw();
    if (g_state.sgr_mouse_active) {
        write_str("\033[?1006l\033[?1000l", 16);
        g_state.sgr_mouse_active = false;
    }
    if (g_raw_active) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_orig_termios);
        write_str("\033[?25h\033[0m\033[?1049l", 18);
        g_raw_active = false;
    }
}

static void handle_signal(int sig) {
    (void)sig;
    restore_terminal();
    _exit(128 + sig);
}

static void enable_raw_mode(void) {
    if (tcgetattr(STDIN_FILENO, &g_orig_termios) == -1) {
        perror("tcgetattr");
        exit(EXIT_FAILURE);
    }
    atexit(restore_terminal);
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    struct termios raw = g_orig_termios;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
        perror("tcsetattr");
        exit(EXIT_FAILURE);
    }
    g_raw_active = true;
    write_str("\033[?1049h\033[?25l", 14);
    sync_mouse_mode();
}

static void brush_load_square(int idx) {
    if (idx < 0) idx = (idx % PALETTE_COUNT + PALETTE_COUNT) % PALETTE_COUNT;
    if (idx >= PALETTE_COUNT) idx = idx % PALETTE_COUNT;
    g_state.selected_square = idx;
    g_state.brush.r = PALETTE[idx].r;
    g_state.brush.g = PALETTE[idx].g;
    g_state.brush.b = PALETTE[idx].b;
    g_state.brush.transparent = false;
}

static void brush_paint_segment(void) {
    int s = g_state.active_seg;
    if (g_state.target_bg) {
        g_state.active_cfg.seg_bg[s] = g_state.brush;
        g_state.active_cfg.seg_bg[s].transparent = false;
    } else {
        g_state.active_cfg.seg_fg[s] = g_state.brush;
    }
}

static void handle_mouse_click(int col, int row) {
    if (row == 8) {
        if (col >= 4 && col <= 18) g_state.active_seg = 0;
        else if (col >= 21 && col <= 35) g_state.active_seg = 1;
        else if (col >= 38 && col <= 52) g_state.active_seg = 2;
        else if (col >= 55 && col <= 69) g_state.active_seg = 3;
        return;
    }
    if (row == 11) {
        if (col >= 28 && col <= 40) g_state.target_bg = false;
        else if (col >= 43 && col <= 55) g_state.target_bg = true;
        else if (col >= 58 && col <= 67) g_state.active_cfg.seg_bg[g_state.active_seg].transparent = true;
        return;
    }
    if ((row == 12 || row == 14) && col >= 4 && col <= 63) {
        int r_idx = (row == 12) ? 0 : 1;
        int c_idx = (col - 4) / 5;
        int sq = r_idx * 12 + c_idx;
        if (sq >= 0 && sq < PALETTE_COUNT) {
            brush_load_square(sq);
            brush_paint_segment();
            snprintf(g_state.status_msg, sizeof(g_state.status_msg),
                     "Painted Seg %d %s with %s",
                     g_state.active_seg + 1, g_state.target_bg ? "BG" : "FG", PALETTE[sq].name);
        }
        return;
    }
    if (row == 17) {
        if (col >= 11 && col <= 21) g_state.active_cfg.style_idx = 0;
        else if (col >= 23 && col <= 31) g_state.active_cfg.style_idx = 1;
        else if (col >= 33 && col <= 41) g_state.active_cfg.style_idx = 2;
        else if (col >= 43 && col <= 49) g_state.active_cfg.style_idx = 3;
        else if (col >= 51 && col <= 61) g_state.active_cfg.style_idx = 4;
        return;
    }
    if (row == 18) {
        int s = g_state.active_seg;
        if (col >= 11 && col <= 17) g_state.active_cfg.seg_icon[s] = 0;
        else if (col >= 19 && col <= 26) g_state.active_cfg.seg_icon[s] = 1;
        else if (col >= 28 && col <= 32) g_state.active_cfg.seg_icon[s] = 2;
        else if (col >= 34 && col <= 39) g_state.active_cfg.seg_icon[s] = 3;
        else if (col >= 41 && col <= 46) g_state.active_cfg.seg_icon[s] = 4;
        else if (col >= 48 && col <= 55) g_state.active_cfg.seg_icon[s] = 5;
        return;
    }
}

static void init_default_profile(ProfileConfig *cfg, bool is_root) {
    memset(cfg, 0, sizeof(*cfg));
    cfg->style_idx = 0;
    cfg->ascii_fallback = (g_state.is_linux_console || g_state.force_tty_ascii);
    cfg->proc_net_enabled = true;
    cfg->euid_square = true;
    for (int i = 0; i < 4; i++) cfg->seg_enabled[i] = true;

    if (is_root) {
        cfg->seg_fg[0] = (RgbColor){0x11, 0x11, 0x1b, false};
        cfg->seg_bg[0] = (RgbColor){0xf3, 0x8b, 0xa8, false};
        cfg->seg_icon[0] = 5;
        cfg->seg_fg[1] = (RgbColor){0xcd, 0xd6, 0xf4, false};
        cfg->seg_bg[1] = (RgbColor){0x31, 0x32, 0x44, false};
        cfg->seg_icon[1] = 1;
        cfg->seg_fg[2] = (RgbColor){0xf9, 0xe2, 0xaf, false};
        cfg->seg_bg[2] = (RgbColor){0x1e, 0x1e, 0x2e, false};
        cfg->seg_icon[2] = 2;
        cfg->seg_fg[3] = (RgbColor){0xf3, 0x8b, 0xa8, false};
        cfg->seg_bg[3] = (RgbColor){0x11, 0x11, 0x1b, true};
        cfg->seg_icon[3] = 3;
    } else {
        cfg->seg_fg[0] = (RgbColor){0x11, 0x11, 0x1b, false};
        cfg->seg_bg[0] = (RgbColor){0x89, 0xb4, 0xfa, false};
        cfg->seg_icon[0] = 0;
        cfg->seg_fg[1] = (RgbColor){0xcd, 0xd6, 0xf4, false};
        cfg->seg_bg[1] = (RgbColor){0x31, 0x32, 0x44, false};
        cfg->seg_icon[1] = 1;
        cfg->seg_fg[2] = (RgbColor){0xa6, 0xe3, 0xa1, false};
        cfg->seg_bg[2] = (RgbColor){0x1e, 0x1e, 0x2e, false};
        cfg->seg_icon[2] = 2;
        cfg->seg_fg[3] = (RgbColor){0xa6, 0xe3, 0xa1, false};
        cfg->seg_bg[3] = (RgbColor){0x11, 0x11, 0x1b, true};
        cfg->seg_icon[3] = 4;
    }
}

static void apply_pipe_pill_preset(ProfileConfig *cfg, bool is_root) {
    cfg->style_idx = 4;
    cfg->ascii_fallback = true;
    cfg->proc_net_enabled = false;
    cfg->euid_square = true;
    cfg->seg_enabled[0] = true;
    cfg->seg_enabled[1] = true;
    cfg->seg_enabled[2] = false;
    cfg->seg_enabled[3] = true;
    cfg->seg_fg[0] = (RgbColor){0x11, 0x11, 0x1b, false};
    cfg->seg_bg[0] = (RgbColor){0xcb, 0xa6, 0xf7, false};
    cfg->seg_fg[1] = (RgbColor){0x11, 0x11, 0x1b, false};
    cfg->seg_bg[1] = (RgbColor){0xcb, 0xa6, 0xf7, false};
    if (is_root) {
        cfg->seg_fg[3] = (RgbColor){0xdc, 0x26, 0x26, false};
        cfg->seg_bg[3] = (RgbColor){0xdc, 0x26, 0x26, false};
    } else {
        cfg->seg_fg[3] = (RgbColor){0x10, 0xb9, 0x81, false};
        cfg->seg_bg[3] = (RgbColor){0x10, 0xb9, 0x81, false};
    }
    snprintf(g_state.status_msg, sizeof(g_state.status_msg),
             "Loaded |$PWD:\\h| | pipe preset (%s status block).",
             is_root ? "red root" : "green user");
}

static void load_existing_state(void) {
    FILE *fp = fopen(g_state.target_path, "r");
    if (!fp) return;
    char line[1024];
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "# PS1CFG_STATE ", 15) == 0 || strncmp(line, "# PROMPT_TUI_STATE ", 19) == 0) {
            const char *payload = (line[2] == 'P' && line[3] == 'S') ? (line + 15) : (line + 19);
            ProfileConfig *c = &g_state.active_cfg;
            int ascii = 0, pnet = 1, esq = 1;
            int t0 = 0, t1 = 0, t2 = 0, t3 = 1;
            int en0 = 1, en1 = 1, en2 = 1, en3 = 1;
            int n = sscanf(payload,
                "style=%d ascii=%d pnet=%d "
                "fg0=%hhu,%hhu,%hhu bg0=%hhu,%hhu,%hhu,%d ic0=%d "
                "fg1=%hhu,%hhu,%hhu bg1=%hhu,%hhu,%hhu,%d ic1=%d "
                "fg2=%hhu,%hhu,%hhu bg2=%hhu,%hhu,%hhu,%d ic2=%d "
                "fg3=%hhu,%hhu,%hhu bg3=%hhu,%hhu,%hhu,%d ic3=%d "
                "en=%d,%d,%d,%d esq=%d",
                &c->style_idx, &ascii, &pnet,
                &c->seg_fg[0].r, &c->seg_fg[0].g, &c->seg_fg[0].b,
                &c->seg_bg[0].r, &c->seg_bg[0].g, &c->seg_bg[0].b, &t0, &c->seg_icon[0],
                &c->seg_fg[1].r, &c->seg_fg[1].g, &c->seg_fg[1].b,
                &c->seg_bg[1].r, &c->seg_bg[1].g, &c->seg_bg[1].b, &t1, &c->seg_icon[1],
                &c->seg_fg[2].r, &c->seg_fg[2].g, &c->seg_fg[2].b,
                &c->seg_bg[2].r, &c->seg_bg[2].g, &c->seg_bg[2].b, &t2, &c->seg_icon[2],
                &c->seg_fg[3].r, &c->seg_fg[3].g, &c->seg_fg[3].b,
                &c->seg_bg[3].r, &c->seg_bg[3].g, &c->seg_bg[3].b, &t3, &c->seg_icon[3],
                &en0, &en1, &en2, &en3, &esq);
            if (n >= 35) {
                c->ascii_fallback = g_state.force_tty_ascii ? true : (ascii != 0);
                c->proc_net_enabled = (pnet != 0);
                c->seg_bg[0].transparent = (t0 != 0);
                c->seg_bg[1].transparent = (t1 != 0);
                c->seg_bg[2].transparent = (t2 != 0);
                c->seg_bg[3].transparent = (t3 != 0);
                if (n >= 39) {
                    c->seg_enabled[0] = (en0 != 0);
                    c->seg_enabled[1] = (en1 != 0);
                    c->seg_enabled[2] = (en2 != 0);
                    c->seg_enabled[3] = (en3 != 0);
                }
                if (n >= 40) c->euid_square = (esq != 0);
                snprintf(g_state.status_msg, sizeof(g_state.status_msg),
                         "Loaded existing configuration from %s", g_state.target_path);
            }
            break;
        }
    }
    fclose(fp);
}

static void ensure_bashrc_hook(void) {
    if (g_state.system_managed || g_state.bashrc_path[0] == '\0') return;
    const char *hook = "[ -f \"$HOME/.custom_prompt.sh\" ] && . \"$HOME/.custom_prompt.sh\"";
    FILE *fp = fopen(g_state.bashrc_path, "r");
    if (fp) {
        char buf[1024];
        while (fgets(buf, sizeof(buf), fp)) {
            if (strstr(buf, ".custom_prompt.sh")) {
                fclose(fp);
                return;
            }
        }
        fclose(fp);
    }
    fp = fopen(g_state.bashrc_path, "a");
    if (fp) {
        fprintf(fp, "\n# Managed by ps1cfg (fragOS-utils)\n%s\n", hook);
        fclose(fp);
    }
}

static void write_ps1_line(FILE *fp, const ProfileConfig *c, bool indent) {
    if (c->style_idx == 4) {
        RgbColor pfg = c->seg_fg[0];
        RgbColor pbg = c->seg_bg[0];
        RgbColor blk = c->seg_bg[3].transparent ? c->seg_fg[3] : c->seg_bg[3];
        fprintf(fp, "%sPS1='\\[\\e[1;38;2;%u;%u;%u", indent ? "  " : "", pfg.r, pfg.g, pfg.b);
        if (!pbg.transparent) fprintf(fp, ";48;2;%u;%u;%u", pbg.r, pbg.g, pbg.b);
        fprintf(fp, "m\\]|");
        bool printed = false;
        if (c->seg_enabled[1]) { fprintf(fp, "$PWD"); printed = true; }
        if (c->seg_enabled[0]) { fprintf(fp, "%s\\h", printed ? ":" : ""); printed = true; }
        if (c->seg_enabled[2] && c->proc_net_enabled) { fprintf(fp, "%s${_NET_STAT}", printed ? ":" : ""); printed = true; }
        if (c->euid_square) {
            fprintf(fp, "|${_EUID_SQ}\\[\\e[1;38;2;%u;%u;%u", pfg.r, pfg.g, pfg.b);
            if (!pbg.transparent) fprintf(fp, ";48;2;%u;%u;%u", pbg.r, pbg.g, pbg.b);
            fprintf(fp, "m\\]");
        } else if (c->seg_enabled[3]) {
            fprintf(fp, "|\\[\\e[48;2;%u;%u;%um\\] \\[\\e[1;38;2;%u;%u;%u", blk.r, blk.g, blk.b, pfg.r, pfg.g, pfg.b);
            if (!pbg.transparent) fprintf(fp, ";48;2;%u;%u;%u", pbg.r, pbg.g, pbg.b);
            fprintf(fp, "m\\]");
        }
        fprintf(fp, "|\\[\\e[0m\\] '\n");
        return;
    }

    const char *sep = c->ascii_fallback ? STYLES[c->style_idx].ascii_sep : STYLES[c->style_idx].nerd_sep;
    const char *tokens[4] = {"\\u@\\h", "\\w", "${_NET_STAT}", g_state.system_managed ? "#" : "\\$"};
    int last_enabled = -1;
    for (int i = 0; i < 4; i++) {
        if (c->seg_enabled[i] && !(i == 2 && !c->proc_net_enabled)) last_enabled = i;
    }

    fprintf(fp, "%sPS1='", indent ? "  " : "");
    for (int i = 0; i < 4; i++) {
        if (!c->seg_enabled[i]) continue;
        if (i == 2 && !c->proc_net_enabled) continue;
        const char *glyph = c->ascii_fallback ? ICONS[c->seg_icon[i]].ascii_glyph : ICONS[c->seg_icon[i]].nerd_glyph;

        fprintf(fp, "\\[\\e[38;2;%u;%u;%um", c->seg_fg[i].r, c->seg_fg[i].g, c->seg_fg[i].b);
        if (!c->seg_bg[i].transparent) {
            fprintf(fp, ";48;2;%u;%u;%u", c->seg_bg[i].r, c->seg_bg[i].g, c->seg_bg[i].b);
        }
        fprintf(fp, "m\\] %s %s \\[\\e[0m\\]", glyph, tokens[i]);
        if (i < last_enabled && !c->seg_bg[i].transparent) {
            fprintf(fp, "\\[\\e[38;2;%u;%u;%um\\]%s\\[\\e[0m\\]",
                    c->seg_bg[i].r, c->seg_bg[i].g, c->seg_bg[i].b, sep);
        }
    }
    if (c->euid_square) {
        fprintf(fp, " ${_EUID_SQ}");
    }
    fprintf(fp, " '\n");
}

static void save_profile_script(void) {
    char tmp_path[540], bak_path[540];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", g_state.target_path);
    snprintf(bak_path, sizeof(bak_path), "%s.bak", g_state.target_path);

    FILE *fp = fopen(tmp_path, "w");
    if (!fp) {
        snprintf(g_state.status_msg, sizeof(g_state.status_msg),
                 "Error writing %s: %s", tmp_path, strerror(errno));
        return;
    }

    const ProfileConfig *c = &g_state.active_cfg;
    fputs("#!/usr/bin/env bash\n", fp);
    fputs("# Auto-managed by ps1cfg (fragOS-utils Zero-Dependency POSIX termios)\n", fp);
    fprintf(fp, "# PS1CFG_STATE style=%d ascii=%d pnet=%d "
                "fg0=%u,%u,%u bg0=%u,%u,%u,%d ic0=%d "
                "fg1=%u,%u,%u bg1=%u,%u,%u,%d ic1=%d "
                "fg2=%u,%u,%u bg2=%u,%u,%u,%d ic2=%d "
                "fg3=%u,%u,%u bg3=%u,%u,%u,%d ic3=%d "
                "en=%d,%d,%d,%d esq=%d\n",
            c->style_idx, c->ascii_fallback ? 1 : 0, c->proc_net_enabled ? 1 : 0,
            c->seg_fg[0].r, c->seg_fg[0].g, c->seg_fg[0].b,
            c->seg_bg[0].r, c->seg_bg[0].g, c->seg_bg[0].b, c->seg_bg[0].transparent ? 1 : 0, c->seg_icon[0],
            c->seg_fg[1].r, c->seg_fg[1].g, c->seg_fg[1].b,
            c->seg_bg[1].r, c->seg_bg[1].g, c->seg_bg[1].b, c->seg_bg[1].transparent ? 1 : 0, c->seg_icon[1],
            c->seg_fg[2].r, c->seg_fg[2].g, c->seg_fg[2].b,
            c->seg_bg[2].r, c->seg_bg[2].g, c->seg_bg[2].b, c->seg_bg[2].transparent ? 1 : 0, c->seg_icon[2],
            c->seg_fg[3].r, c->seg_fg[3].g, c->seg_fg[3].b,
            c->seg_bg[3].r, c->seg_bg[3].g, c->seg_bg[3].b, c->seg_bg[3].transparent ? 1 : 0, c->seg_icon[3],
            c->seg_enabled[0] ? 1 : 0, c->seg_enabled[1] ? 1 : 0,
            c->seg_enabled[2] ? 1 : 0, c->seg_enabled[3] ? 1 : 0,
            c->euid_square ? 1 : 0);

    fputs("\n[[ $- != *i* ]] && return\n\n", fp);

    if (c->euid_square) {
        fputs("_ps1cfg_euid_square() {\n", fp);
        fputs("  if [[ \"${EUID:-$(id -u)}\" -eq 0 ]]; then\n", fp);
        fputs("    _EUID_SQ=$'\\001\\e[48;2;220;38;38m\\002 \\001\\e[0m\\002'\n", fp);
        fputs("  else\n", fp);
        fputs("    _EUID_SQ=$'\\001\\e[48;2;16;185;129m\\002 \\001\\e[0m\\002'\n", fp);
        fputs("  fi\n", fp);
        fputs("}\n\n", fp);
    }

    bool use_pnet = c->proc_net_enabled && c->seg_enabled[2];
    if (use_pnet) {
        fputs("_prompt_net_telemetry() {\n", fp);
        fputs("  local est=0 lst=0 gw='-' line iface dest gw_hex\n", fp);
        fputs("  if [[ -r /proc/net/tcp ]]; then\n", fp);
        fputs("    while read -r _ _ _ st _; do\n", fp);
        fputs("      [[ \"$st\" == \"01\" ]] && ((est++))\n", fp);
        fputs("      [[ \"$st\" == \"0A\" ]] && ((lst++))\n", fp);
        fputs("    done < /proc/net/tcp\n", fp);
        fputs("  fi\n", fp);
        fputs("  if [[ -r /proc/net/route ]]; then\n", fp);
        fputs("    while read -r iface dest gw_hex _; do\n", fp);
        fputs("      if [[ \"$dest\" == \"00000000\" && ${#gw_hex} -eq 8 ]]; then\n", fp);
        fputs("        printf -v gw '%d.%d.%d.%d' \"0x${gw_hex:6:2}\" \"0x${gw_hex:4:2}\" \"0x${gw_hex:2:2}\" \"0x${gw_hex:0:2}\"\n", fp);
        fputs("        break\n", fp);
        fputs("      fi\n", fp);
        fputs("    done < /proc/net/route\n", fp);
        fputs("  fi\n", fp);
        fputs("  _NET_STAT=\"TCP:${est}/${lst} GW:${gw}\"\n", fp);
        fputs("}\n\n", fp);
    }

    if (g_state.system_managed) {
        fputs("if [[ \"${EUID:-$(id -u)}\" -eq 0 ]]; then\n", fp);
        if (use_pnet && c->euid_square) {
            fputs("  PROMPT_COMMAND='_ps1cfg_euid_square; _prompt_net_telemetry'\n", fp);
        } else if (use_pnet) {
            fputs("  PROMPT_COMMAND='_prompt_net_telemetry'\n", fp);
        } else if (c->euid_square) {
            fputs("  PROMPT_COMMAND='_ps1cfg_euid_square'\n", fp);
        }
        write_ps1_line(fp, c, true);
        fputs("fi\n", fp);
    } else {
        if (use_pnet && c->euid_square) {
            fputs("PROMPT_COMMAND='_ps1cfg_euid_square; _prompt_net_telemetry'\n", fp);
        } else if (use_pnet) {
            fputs("PROMPT_COMMAND='_prompt_net_telemetry'\n", fp);
        } else if (c->euid_square) {
            fputs("PROMPT_COMMAND='_ps1cfg_euid_square'\n", fp);
        }
        write_ps1_line(fp, c, false);
    }

    fclose(fp);
    rename(g_state.target_path, bak_path);
    if (rename(tmp_path, g_state.target_path) != 0) {
        snprintf(g_state.status_msg, sizeof(g_state.status_msg),
                 "Atomic rename failed: %s", strerror(errno));
        return;
    }
    chmod(g_state.target_path, 0644);
    ensure_bashrc_hook();
    snprintf(g_state.status_msg, sizeof(g_state.status_msg),
             "Saved & applied to %s (run: source %s)", g_state.target_path, g_state.target_path);
}

static void uninstall_script(void) {
    if (unlink(g_state.target_path) == 0) {
        snprintf(g_state.status_msg, sizeof(g_state.status_msg),
                 "Uninstalled %s — default shell prompt restored.", g_state.target_path);
    } else {
        snprintf(g_state.status_msg, sizeof(g_state.status_msg),
                 "Nothing to uninstall at %s (%s)", g_state.target_path, strerror(errno));
    }
}

static const char *get_mouse_status_label(void) {
    if (!g_state.mouse_enabled) return "OFF (Keyboard)";
    if (g_state.gpm_fd >= 0) return "GPM (/dev/gpmctl)";
    if (g_state.sgr_mouse_active) return "XTerm SGR";
    return "Keyboard Only";
}

static void draw_box_top(int row, const char *title, bool ascii) {
    const char *tl = ascii ? "+" : "\xe2\x95\xad";
    const char *tr = ascii ? "+" : "\xe2\x95\xae";
    const char *hz = ascii ? "-" : "\xe2\x94\x80";
    int title_len = (int)strlen(title);
    printf("\033[%d;1H\033[38;2;88;91;112m%s%s \033[1;38;2;137;180;250m%s\033[0;38;2;88;91;112m ", row, tl, hz, title);
    int used = 4 + title_len;
    for (int i = used; i < TUI_BOX_WIDTH - 1; i++) fputs(hz, stdout);
    printf("%s\033[0m", tr);
}

static void draw_box_bot(int row, bool ascii) {
    const char *bl = ascii ? "+" : "\xe2\x95\xb0";
    const char *br = ascii ? "+" : "\xe2\x95\xaf";
    const char *hz = ascii ? "-" : "\xe2\x94\x80";
    printf("\033[%d;1H\033[38;2;88;91;112m%s", row, bl);
    for (int i = 1; i < TUI_BOX_WIDTH - 1; i++) fputs(hz, stdout);
    printf("%s\033[0m", br);
}

static void draw_box_sides(int row, bool ascii) {
    const char *vt = ascii ? "|" : "\xe2\x94\x82";
    printf("\033[%d;1H\033[38;2;88;91;112m%s\033[0m\033[%d;%dH\033[38;2;88;91;112m%s\033[0m",
           row, vt, row, TUI_BOX_WIDTH, vt);
}

static void render_ui(void) {
    const ProfileConfig *c = &g_state.active_cfg;
    bool box_ascii = c->ascii_fallback || g_state.force_tty_ascii;
    const char *chk = box_ascii ? "*" : "\xe2\x9c\x93";

    printf("\033[H\033[2J");

    draw_box_top(1, "ps1cfg v" PS1CFG_VERSION " — fragOS-utils Bash Prompt Studio", box_ascii);
    draw_box_sides(2, box_ascii);
    printf("\033[2;4HContext: \033[1;38;2;%s\033[0m   Env: \033[1m%s\033[0m   Mouse [M]: \033[1;38;2;116;199;236m%s\033[0m",
           g_state.system_managed ? "243;139;168mroot (#)" : "166;227;161muser ($)",
           g_state.no_gui_env ? "Pure TTY" : "GUI/Term",
           get_mouse_status_label());
    draw_box_bot(3, box_ascii);

    draw_box_top(4, "Live Prompt Preview", box_ascii);
    draw_box_sides(5, box_ascii);
    printf("\033[5;4H");
    const char *sample[4] = {g_state.system_managed ? "root@host" : "user@host", "~/projects", "TCP:4/2", g_state.system_managed ? "#" : "$"};
    const char *sep = c->ascii_fallback ? STYLES[c->style_idx].ascii_sep : STYLES[c->style_idx].nerd_sep;
    int last_enabled = -1;
    for (int i = 0; i < 4; i++) {
        if (c->seg_enabled[i] && !(i == 2 && !c->proc_net_enabled)) last_enabled = i;
    }
    if (c->style_idx == 4) {
        RgbColor pfg = c->seg_fg[0];
        RgbColor pbg = c->seg_bg[0];
        RgbColor blk = c->euid_square
            ? (g_state.system_managed ? (RgbColor){0xdc, 0x26, 0x26, false} : (RgbColor){0x10, 0xb9, 0x81, false})
            : (c->seg_bg[3].transparent ? c->seg_fg[3] : c->seg_bg[3]);
        printf("\033[1;38;2;%u;%u;%u", pfg.r, pfg.g, pfg.b);
        if (!pbg.transparent) printf(";48;2;%u;%u;%u", pbg.r, pbg.g, pbg.b);
        printf("m|");
        bool printed = false;
        if (c->seg_enabled[1]) { printf("/home/user/projects"); printed = true; }
        if (c->seg_enabled[0]) { printf("%shost", printed ? ":" : ""); printed = true; }
        if (c->seg_enabled[2] && c->proc_net_enabled) { printf("%sTCP:4/2", printed ? ":" : ""); printed = true; }
        if (c->euid_square || c->seg_enabled[3]) {
            printf("|\033[48;2;%u;%u;%um \033[1;38;2;%u;%u;%u", blk.r, blk.g, blk.b, pfg.r, pfg.g, pfg.b);
            if (!pbg.transparent) printf(";48;2;%u;%u;%u", pbg.r, pbg.g, pbg.b);
            printf("m");
        }
        printf("|\033[0m ");
    } else {
        for (int i = 0; i < 4; i++) {
            if (!c->seg_enabled[i]) continue;
            if (i == 2 && !c->proc_net_enabled) continue;
            const char *glyph = c->ascii_fallback ? ICONS[c->seg_icon[i]].ascii_glyph : ICONS[c->seg_icon[i]].nerd_glyph;
            printf("\033[38;2;%u;%u;%um", c->seg_fg[i].r, c->seg_fg[i].g, c->seg_fg[i].b);
            if (!c->seg_bg[i].transparent) printf("\033[48;2;%u;%u;%um", c->seg_bg[i].r, c->seg_bg[i].g, c->seg_bg[i].b);
            printf(" %s %s \033[0m", glyph, sample[i]);
            if (i < last_enabled && !c->seg_bg[i].transparent) {
                printf("\033[38;2;%u;%u;%um%s\033[0m", c->seg_bg[i].r, c->seg_bg[i].g, c->seg_bg[i].b, sep);
            }
        }
        if (c->euid_square) {
            if (g_state.system_managed) printf(" \033[48;2;220;38;38m \033[0m");
            else printf(" \033[48;2;16;185;129m \033[0m");
        }
        if (last_enabled == -1 && !c->euid_square) printf("\033[2m(all segments hidden — press V to show)\033[0m");
    }
    draw_box_sides(5, box_ascii);
    draw_box_bot(6, box_ascii);

    draw_box_top(7, "1. Select & Toggle Prompt Segments [1-4 / Tab / V]", box_ascii);
    draw_box_sides(8, box_ascii);
    const char *seg_labels[4] = {"1:User@Host", "2:Directory", "3:/proc/net", "4:Symbol   "};
    int seg_cols[4] = {4, 21, 38, 55};
    for (int i = 0; i < 4; i++) {
        bool sel = (i == g_state.active_seg);
        bool on = c->seg_enabled[i];
        printf("\033[8;%dH", seg_cols[i]);
        if (sel) {
            printf("\033[1;38;2;17;17;27;48;2;137;180;250m %s %s \033[0m", seg_labels[i], on ? chk : "-");
        } else if (on) {
            printf("\033[38;2;205;214;244;48;2;49;50;68m %s %s \033[0m", seg_labels[i], chk);
        } else {
            printf("\033[38;2;108;112;134;48;2;30;30;46m %s - \033[0m", seg_labels[i]);
        }
    }
    draw_box_bot(9, box_ascii);

    draw_box_top(10, "2. Color Brush Palette [Arrows / Enter / Click]", box_ascii);
    draw_box_sides(11, box_ascii);
    printf("\033[11;4HBrush: \033[48;2;%u;%u;%um   \033[0m #%02X%02X%02X",
           g_state.brush.r, g_state.brush.g, g_state.brush.b,
           g_state.brush.r, g_state.brush.g, g_state.brush.b);
    printf("\033[11;28H%s[F: Paint FG]\033[0m  %s[B: Paint BG]\033[0m  \033[38;2;166;173;200m[C: No BG]\033[0m",
           !g_state.target_bg ? "\033[1;38;2;17;17;27;48;2;137;180;250m" : "\033[38;2;166;173;200;48;2;49;50;68m",
           g_state.target_bg ? "\033[1;38;2;17;17;27;48;2;137;180;250m" : "\033[38;2;166;173;200;48;2;49;50;68m");

    draw_box_sides(12, box_ascii);
    draw_box_sides(13, box_ascii);
    draw_box_sides(14, box_ascii);
    for (int i = 0; i < PALETTE_COUNT; i++) {
        int r = (i < 12) ? 12 : 14;
        int col = 4 + (i % 12) * 5;
        printf("\033[%d;%dH", r, col);
        if (i == g_state.selected_square) {
            printf("\033[1;38;2;249;226;175m[\033[48;2;%u;%u;%um   \033[0;1;38;2;249;226;175m]\033[0m",
                   PALETTE[i].r, PALETTE[i].g, PALETTE[i].b);
        } else {
            printf(" \033[48;2;%u;%u;%um   \033[0m ", PALETTE[i].r, PALETTE[i].g, PALETTE[i].b);
        }
    }
    draw_box_bot(15, box_ascii);

    draw_box_top(16, "3. Style [T] & 4. Segment Icon [I]", box_ascii);
    draw_box_sides(17, box_ascii);
    printf("\033[17;4HStyle: ");
    for (int i = 0; i < STYLE_COUNT; i++) {
        bool active = (i == c->style_idx);
        printf("%s[%s]\033[0m ",
               active ? "\033[1;38;2;17;17;27;48;2;137;180;250m" : "\033[38;2;166;173;200;48;2;49;50;68m",
               STYLES[i].name);
    }
    draw_box_sides(18, box_ascii);
    printf("\033[18;4HIcon:  ");
    for (int i = 0; i < ICON_COUNT; i++) {
        bool active = (i == c->seg_icon[g_state.active_seg]);
        printf("%s[%s]\033[0m ",
               active ? "\033[1;38;2;17;17;27;48;2;137;180;250m" : "\033[38;2;166;173;200;48;2;49;50;68m",
               ICONS[i].label);
    }
    draw_box_bot(19, box_ascii);

    printf("\033[20;2H\033[38;2;203;166;247m[P] Pipe Pill\033[0m  \033[38;2;16;185;129m[U] EUID Square: \033[1m%s\033[0m  [A] Glyphs: \033[1m%s\033[0m  [N] /proc/net: \033[1m%s\033[0m",
           c->euid_square ? "ON" : "OFF",
           c->ascii_fallback ? "ASCII" : "Nerd",
           (c->proc_net_enabled && c->seg_enabled[2]) ? "ON" : "OFF");
    printf("\033[21;2H\033[1;38;2;166;227;161m[S] Save & Apply\033[0m   \033[38;2;243;139;168m[X] Uninstall\033[0m   [V] Show/Hide   [M] Mouse   [Q/Esc] Quit");
    printf("\033[22;2H\033[38;2;166;227;161m> %s\033[0m", g_state.status_msg);
    fflush(stdout);
}

static bool stdin_has_more(int timeout_ms) {
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(STDIN_FILENO, &rfds);
    struct timeval tv = { timeout_ms / 1000, (timeout_ms % 1000) * 1000 };
    return select(STDIN_FILENO + 1, &rfds, NULL, NULL, &tv) > 0;
}

static void parse_sgr_mouse(void) {
    char buf[32];
    int len = 0;
    while (len < (int)sizeof(buf) - 1 && stdin_has_more(20)) {
        char c;
        if (read(STDIN_FILENO, &c, 1) != 1) break;
        buf[len++] = c;
        if (c == 'M' || c == 'm') break;
    }
    buf[len] = '\0';
    int btn = 0, col = 0, row = 0;
    char kind = 0;
    if (sscanf(buf, "%d;%d;%d%c", &btn, &col, &row, &kind) == 4) {
        if (kind == 'M' && (btn & 3) == 0) {
            handle_mouse_click(col, row);
        }
    }
}

int main(int argc, char **argv) {
    memset(&g_state, 0, sizeof(g_state));
    g_state.gpm_fd = -1;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_help(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
            printf("ps1cfg v%s (fragOS-utils Zero-Dependency POSIX termios + GPM)\n", PS1CFG_VERSION);
            return 0;
        } else if (strcmp(argv[i], "--help-html") == 0) {
            const char *dest = (i + 1 < argc && argv[i + 1][0] != '-') ? argv[i + 1] : NULL;
            export_help_html(dest);
            return 0;
        } else if (strcmp(argv[i], "--gpm") == 0) {
            g_state.gpm_flag = true;
        } else if (strcmp(argv[i], "--tty") == 0) {
            g_state.force_tty_ascii = true;
        } else {
            fprintf(stderr, "Unknown option: %s\nTry '%s --help' for more information.\n", argv[i], argv[0]);
            return 1;
        }
    }

    detect_environment();
    g_state.mouse_enabled = g_state.gpm_flag || !g_state.no_gui_env;
    resolve_managed_paths();
    init_default_profile(&g_state.active_cfg, g_state.system_managed);
    brush_load_square(8);
    snprintf(g_state.status_msg, sizeof(g_state.status_msg),
             "Ready. Target: %s (Arrows/Tab/Enter%s)",
             g_state.target_path, g_state.mouse_enabled ? " or Mouse" : "");
    load_existing_state();

    enable_raw_mode();
    while (1) {
        render_ui();

        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(STDIN_FILENO, &rfds);
        int maxfd = STDIN_FILENO;
        if (g_state.gpm_fd >= 0) {
            FD_SET(g_state.gpm_fd, &rfds);
            if (g_state.gpm_fd > maxfd) maxfd = g_state.gpm_fd;
        }
        if (select(maxfd + 1, &rfds, NULL, NULL, NULL) <= 0) continue;

        if (g_state.gpm_fd >= 0 && FD_ISSET(g_state.gpm_fd, &rfds)) {
            GpmEventRaw ev;
            ssize_t n = read(g_state.gpm_fd, &ev, sizeof(ev));
            if (n <= 0) {
                gpm_close_raw();
            } else if (n == (ssize_t)sizeof(ev) && (ev.type & GPM_EVT_DOWN) && (ev.buttons & GPM_BTN_LEFT)) {
                handle_mouse_click(ev.x, ev.y);
            }
            continue;
        }

        if (!FD_ISSET(STDIN_FILENO, &rfds)) continue;
        char ch;
        if (read(STDIN_FILENO, &ch, 1) != 1) continue;

        if (ch == '\033') {
            if (!stdin_has_more(25)) break;
            char seq;
            if (read(STDIN_FILENO, &seq, 1) != 1) continue;
            if (seq == '[' && stdin_has_more(25)) {
                char code;
                if (read(STDIN_FILENO, &code, 1) != 1) continue;
                if (code == 'A') {
                    brush_load_square(g_state.selected_square - 12);
                    brush_paint_segment();
                } else if (code == 'B') {
                    brush_load_square(g_state.selected_square + 12);
                    brush_paint_segment();
                } else if (code == 'C') {
                    brush_load_square(g_state.selected_square + 1);
                    brush_paint_segment();
                } else if (code == 'D') {
                    brush_load_square(g_state.selected_square - 1);
                    brush_paint_segment();
                } else if (code == 'Z') {
                    g_state.active_seg = (g_state.active_seg + 3) % 4;
                } else if (code == '<') {
                    parse_sgr_mouse();
                }
            }
            continue;
        }

        if (ch == 'q' || ch == 'Q') break;
        else if (ch == '\t') g_state.active_seg = (g_state.active_seg + 1) % 4;
        else if (ch == '\r' || ch == '\n' || ch == ' ') brush_paint_segment();
        else if (ch >= '1' && ch <= '4') g_state.active_seg = ch - '1';
        else if (ch == 'v' || ch == 'V') {
            int s = g_state.active_seg;
            g_state.active_cfg.seg_enabled[s] = !g_state.active_cfg.seg_enabled[s];
            if (s == 2) g_state.active_cfg.proc_net_enabled = g_state.active_cfg.seg_enabled[2];
        }
        else if (ch == 'f' || ch == 'F') g_state.target_bg = false;
        else if (ch == 'b' || ch == 'B') g_state.target_bg = true;
        else if (ch == 'c' || ch == 'C') g_state.active_cfg.seg_bg[g_state.active_seg].transparent = true;
        else if (ch == 'h' || ch == 'H') {
            brush_load_square(g_state.selected_square - 1);
            brush_paint_segment();
        } else if (ch == 'l' || ch == 'L') {
            brush_load_square(g_state.selected_square + 1);
            brush_paint_segment();
        } else if (ch == 't' || ch == 'T') {
            g_state.active_cfg.style_idx = (g_state.active_cfg.style_idx + 1) % STYLE_COUNT;
        } else if (ch == 'p' || ch == 'P') {
            apply_pipe_pill_preset(&g_state.active_cfg, g_state.system_managed);
        } else if (ch == 'u' || ch == 'U') {
            g_state.active_cfg.euid_square = !g_state.active_cfg.euid_square;
            snprintf(g_state.status_msg, sizeof(g_state.status_msg),
                     "User/Root EUID Status Square: %s",
                     g_state.active_cfg.euid_square ? "ON (green user / red root)" : "OFF");
        } else if (ch == 'i' || ch == 'I') {
            int s = g_state.active_seg;
            g_state.active_cfg.seg_icon[s] = (g_state.active_cfg.seg_icon[s] + 1) % ICON_COUNT;
        } else if (ch == 'a' || ch == 'A') {
            g_state.active_cfg.ascii_fallback = !g_state.active_cfg.ascii_fallback;
        } else if (ch == 'n' || ch == 'N') {
            g_state.active_cfg.proc_net_enabled = !g_state.active_cfg.proc_net_enabled;
            g_state.active_cfg.seg_enabled[2] = g_state.active_cfg.proc_net_enabled;
        } else if (ch == 'm' || ch == 'M') {
            g_state.mouse_enabled = !g_state.mouse_enabled;
            sync_mouse_mode();
            snprintf(g_state.status_msg, sizeof(g_state.status_msg),
                     "Mouse mode: %s", get_mouse_status_label());
        } else if (ch == 's' || ch == 'S') {
            save_profile_script();
        } else if (ch == 'x' || ch == 'X') {
            uninstall_script();
        }
    }

    restore_terminal();
    return 0;
}
