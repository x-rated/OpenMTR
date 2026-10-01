# GUI Internals (`MainWindow.h` / `MainWindow.cpp`)

~6300 lines combined, ~27% comments — most "why" questions about the UI are
already answered in-place; this doc is a map to the right section, not a
replacement for reading it. `MainWindow.h` is declared bottom-up (its own
file header lists the order): Windows accent-colour helpers → reusable
painting/style primitives → the Win11-style tooltip → custom title bar →
results-table building blocks → `MainWindow` itself → the modal "Mica"
dialog.

## The big picture: a fully custom, frameless window

OpenMTR draws its own window chrome on every platform except macOS (which
keeps the native title bar — see below). This is the source of most of the
complexity in this file. Three different strategies per platform:

### Windows
Keeps the **native frame**, hidden via `WM_NCCALCSIZE` handling in
`nativeEvent()` — the native frame (and its DWM shadow, Snap Layouts,
rounded corners, drag/resize behavior) is still technically there, just told
its client area is the whole window. `applyFramelessStyle()` removes
`WS_CAPTION`; `nativeEvent()` intercepts `WM_NCHITTEST` for custom hit-testing
of the drawn caption/buttons. One documented gotcha: the very first
`WM_NCCALCSIZE` fires *before* `WS_CAPTION` is removed, so a plain
`resize(1200, 550)` at construction gets sized as if a standard caption+
border would eat into it — then never does. Corrected once the frame
situation settles (`MainWindow.cpp` ~L1649).

### Linux (X11/XWayland)
Fully frameless (`Qt::FramelessWindowHint`, no native-frame trick available)
— paints its own rounded background, border, and **hand-painted drop
shadow** (`paintCardShadow()`, layered rounded rects with quadratically
fading alpha — a cheap stand-in for a real Gaussian blur, tuned to match
DWM's own shadow including the focus-dependent depth change). Why hand-paint
a shadow at all: no single mechanism every current Linux desktop provides —
GNOME's Mutter never draws one for an undecorated window, KWin's shadow
protocol needs either a KWayland dependency or (X11) Plasma 6.8+.

- `kShadowMargin` (24px) is a transparent gutter reserved around the visible
  "card" for that shadow. It collapses to 0 while maximized or when an edge
  sits flush against the screen (tiling/snap window managers dock windows
  this way without ever setting a maximized state — detected via
  `currentGutter()`).
- Because the gutter is transparent but the window's input region defaults
  to its full bounding rect, clicks in the gutter would eat input meant for
  whatever's behind the window. Fixed by talking XCB directly
  (`updateLinuxInputShape()`) to set the X Shape **input** region separately
  from the **bounding** region Qt's `setMask()` controls — no public Qt API
  for the input region exists. Skipped entirely under native Wayland (no
  public hook either; harmless — Wayland compositors don't have this
  click-eating problem the same way).
- Resize-by-dragging-an-edge is implemented by hand
  (`ovEdgesAt()`/`eventFilter()`'s edge-detection branch) since there's no
  native frame to provide it. The XCB input region is deliberately shipped
  as a band matching `ovResizeBandInset()` so the clickable pixels and the
  "this counts as an edge grab" pixels can never drift apart.
- Dark/light mode and accent color come from `org.freedesktop.portal.Settings`
  over D-Bus (`onPortalSettingChanged()`), *not* `QStyleHints::colorScheme()`
  alone — that Qt API is unreliable on several distros/desktops (notably
  GNOME; may never fire even though the OS setting changes). The portal path
  is the reliable one; `QStyleHints` is used as a secondary trigger where
  available.

### macOS
Keeps the **native NSWindow title bar** — no frameless setup at all there.
`m_titleBar` is null on this platform. Dark mode is set directly via the
Cocoa runtime (`setMacOsDarkAppearance()`, no Objective-C++ file — raw
`objc_msgSend`), and the live elapsed-time display uses the *native*
`NSWindow.subtitle` (macOS 11+) rather than a custom label, matching native
window chrome instead of imitating the WinUI look used elsewhere. Because
the constructor's `resize(1200, 550)` only sizes *content* (the native title
bar isn't accounted for), a one-time correction shrinks content once the
real native frame geometry is known (`applyFramelessStyle()`'s macOS branch,
`MainWindow.cpp` ~L1666) — guarded to run only once, since it re-runs from
`changeEvent()` on every `WindowStateChange` and would otherwise stomp on a
user resize after minimizing/restoring.

Accent color reads `NSColor.controlAccentColor` via the Cocoa runtime
directly (`macOsSystemAccentColor()`), with a live-update observer
(`installMacOsAccentColorObserver()`) since AppKit's notification has no
direct Qt/MainWindow target — routed through `QMetaObject::invokeMethod`
by method *name* (string-based) since the observer is a plain C function,
not a MainWindow member, so it can't take the method's address directly.

App menu: macOS expects About/Quit in the top-of-screen application menu.
`installMacMenuBar()` creates a parentless `QMenuBar` (which Qt promotes to
the shared native menu bar on macOS only) and uses `QAction::AboutRole` to
relocate the About action into it regardless of which menu it was added to.

## Theming

Two full stylesheets (`applyDarkTheme()`/`applyLightTheme()`), built from
WinUI3 Fluent design tokens where possible (comments cite the specific
token name, e.g. `ControlStrokeColorOnAccentDefault`, and note where a value
was *sampled* from a real Windows 11 screenshot instead of derived from a
token, e.g. the `#202020`/`#f3f3f3` central-widget fills). One recurring
technique: **flatten translucent tokens onto an opaque base**
(`ovAccentBlend()`) before using them as a border color — Qt draws any
border with alpha < 1 through its antialiased rounded-rect path, which
visibly shifts the button half a pixel and softens edges.

Accent color is read fresh per-platform (registry on Windows, `NSColor` on
macOS, the D-Bus portal on Linux) each time a theme is (re-)applied, with a
sensible built-in fallback if the platform read fails for any reason
(missing class/selector, no portal running, unexpected reply shape — never
asserts or crashes on a read failure).

## Native dialogs (Export)

Three completely different code paths per platform, on purpose:

- **Windows**: the Common Item Dialog (`IFileSaveDialog`) — the native picker,
  gets the shell's own most-recently-used directory behavior for free. It is
  used instead of `GetSaveFileNameW` because only it tells the app when the
  "Save as type" entry changes, and the name box follows that change
  (`name.txt` → `name.csv`); `GetSaveFileNameW` leaves the name as it is, and
  a hook to learn about the change would bring back the pre-Vista dialog.
- **macOS**: `QFileDialog::getSaveFileName()` with `DontUseNativeDialog`
  **not** set, which gets Qt's Cocoa plugin to produce the real `NSSavePanel`
  (sidebar, iCloud, tags, recent places) — the Linux-style hand-styled Qt
  dialog looked nothing like a native macOS panel when tried there.
- **Linux**: Qt's own `QFileDialog`, hand-styled — no native save-panel API
  to call into on Linux. Has its own hazards, both real bugs found by
  testing: (1) `QFileDialog` enables a `QSizeGrip` by default even though
  nothing here turns it on, which under the Fusion style draws a diagonal
  square on top of the Cancel button — explicitly disabled; (2) this dialog
  inherits `MainWindow`'s `"QWidget { background-color: transparent }"` rule
  through Qt's style-sheet **parent-chain cascade** (not just the visible
  containment tree), which without real window transparency set up
  collapses to solid black — fixed with an explicit same-selector stylesheet
  on the dialog itself, which wins by cascade order.

All three: an **absolute** starting directory is always passed, never a bare
name — a desktop-launched app (Finder/Dock/`open` on macOS) has `/` as its
working directory, so a relative default silently opens the dialog on the
read-only system volume.

## Copy button feedback & clipboard

Copying is otherwise completely silent, so `m_copyBtn` briefly swaps its own
label to "Copied" rather than showing a dialog/toast — no extra widget,
can't obscure results, the user's eye is already there. **Gotcha**: this
static Qt build's own clipboard write is broken, so `QLineEdit`'s built-in
Ctrl+C silently does nothing — every copy in this app (including inside text
fields) goes through a hand-rolled Win32 `copyTextToClipboard()` instead
(`MainWindow.cpp` ~L2206). If you see clipboard content silently not
updating, check whether the code path in question bypassed this and used
Qt's own clipboard API.

## Report generation reuses the shared core — don't duplicate

`buildJsonExport()`/`buildTextExport()`/the CSV branch of `onExport()` all
call straight into `report_core.h`'s shared formatters — the exact same
functions cli.cpp's `--report` uses. **`m_finalRows` must be snapshotted at
Stop, before `m_asnCache` is cleared** — recomputing rows afterward from a
cleared cache would silently turn every ASN back into `-` and re-trigger
lookups for a trace that has already ended (`MainWindow.cpp` ~L2711,
~L3032).

## Modal dialogs (`MicaDialog`)

Windows-11-styled modal used for About and error messages: rounded card,
dimmed backdrop, themed close button, keyboard focus ring. On Windows, both
rounding and the dimmed backdrop's exact bounds come from
`DwmSetWindowAttribute`'s corner-preference call at the compositor level; on
Linux there's no such call, so `FooterWidget` (the dialog's bottom edge)
paints its own matching rounded corners by hand, and `SmokeOverlay` (the
Linux-only dimmed backdrop) paints its own antialiased rounded-rect fill
rather than using `QWidget::setMask()` — a mask is a binary, non-antialiased
region, so the polygon approximation for the arc never quite lines up with
the antialiased corner `MainWindow::paintEvent()` draws underneath,
producing a visible mismatched-pixel ring at each corner.

Tab order in the dialog footer follows visual layout (link(s) first, then
buttons in on-screen order) and real keyboard focus is deliberately left on
Close even though the accent button is the `Enter`-triggered default action
— so the first Tab press moves focus onto the accent button and shows its
ring, rather than starting there and immediately tabbing away.

## Where the "spacer columns" and column enum live

The `Column` enum and `COLUMNS` header strings are defined in
`report_core.h` (shared with the CLI). MainWindow adds two more trailing
*rendering-only* spacer columns (`ColCount`, `ColCount + 1`) purely for
left-edge table padding — these are not part of the report itself and must
not be confused with real data columns when touching table code.
