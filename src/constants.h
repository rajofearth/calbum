#pragma once
// =========================================================================
// constants.h — Application-wide named constants (theme, fonts, layout, DWM)
//
// Every subsystem that needs a colour, size, font name, glyph, or DWM
// attribute should pull it from here rather than hard-coding it inline.
// =========================================================================

// ── Window ────────────────────────────────────────────────────────────
#define WINDOW_CLASS_NAME L"calbumWindow"
#define WINDOW_TITLE L"calbum"
#define WINDOW_DEFAULT_W 1200
#define WINDOW_DEFAULT_H 800

// ── Zoom & Pan ────────────────────────────────────────────────────────
#define ZOOM_MIN 1.0F
#define ZOOM_MAX 8.0F
#define ZOOM_STEP 1.1F
#define ZOOM_BADGE_W 120.0F
#define ZOOM_BADGE_H 30.0F
#define ZOOM_BADGE_Y 20.0F
#define ZOOM_BADGE_TIMER 2.0F

// ── Theme Colors (RGBA float components) ──────────────────────────────
// Amber / Warm Orange accent
#define ACCENT_R 0.961F
#define ACCENT_G 0.620F
#define ACCENT_B 0.043F
#define ACCENT_A 1.0F

// Obsidian background (#0a0b0d)
#define BG_R 0.039F
#define BG_G 0.043F
#define BG_B 0.051F
#define BG_A 1.0F

// Dark panel (#14161b)
#define PANEL_R 0.078F
#define PANEL_G 0.086F
#define PANEL_B 0.106F
#define PANEL_A 1.0F

// Dark border (#22252c)
#define BORDER_R 0.133F
#define BORDER_G 0.145F
#define BORDER_B 0.173F
#define BORDER_A 1.0F

// Main text (#dce0e5)
#define TEXT_MAIN_R 0.863F
#define TEXT_MAIN_G 0.878F
#define TEXT_MAIN_B 0.898F
#define TEXT_MAIN_A 1.0F

// Muted text (#a9afbc)
#define TEXT_MUTED_R 0.663F
#define TEXT_MUTED_G 0.686F
#define TEXT_MUTED_B 0.737F
#define TEXT_MUTED_A 1.0F

// Scrollbar thumb (#c8ccd4 at 30 %)
#define SCROLLBAR_R 0.784F
#define SCROLLBAR_G 0.800F
#define SCROLLBAR_B 0.831F
#define SCROLLBAR_A 0.30F

// ── Font Families ─────────────────────────────────────────────────────
#define FONT_UI L"Segoe UI Variable"
#define FONT_ICONS L"Segoe Fluent Icons"
#define FONT_MONO_FALLBACK L"Consolas"
#define FONT_MONO_ALT1 L"Zed Mono"
#define FONT_MONO_ALT2 L"Lilex"

// ── Font Sizes ────────────────────────────────────────────────────────
#define FONT_SIZE_UI 15.0F
#define FONT_SIZE_SMALL 11.5F
#define FONT_SIZE_MONO 14.0F
#define FONT_SIZE_MONO_SMALL 11.0F
#define FONT_SIZE_ICON 18.0F
#define FONT_SIZE_ICON_LARGE 48.0F

// ── Icon Glyphs (Segoe Fluent Icons) ──────────────────────────────────
#define GLYPH_BACK L"\uE72B"
#define GLYPH_INFO L"\uE946"
