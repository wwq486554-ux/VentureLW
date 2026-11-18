# User Interface

Player-facing features of the platform: what the player sees, how it is
controlled and how it is configured.  This document follows the platform's
bilingual documentation convention - the Chinese section at the bottom is a
complete translation of the English one, with the same heading structure.  INI
key names, command names, log lines and engine identifiers stay in English in
both sections.

## Tactical view zoom

- The tactical map is rendered at a resolution of its own, so it can be zoomed in
  and out while the **interface keeps the resolution the game is set to**: the
  sidebar, the fonts, the buttons and the menus never scale with the map.
- Zooming in (`k < 1`) keeps the native viewport and magnifies a centred crop of
  it, so it costs no additional engine rendering.  Zooming out (`k > 1`) really
  renders a larger viewport - the engine draws the cells that were outside the
  native view - and the finished frame is scaled back down into the map area.
  Units and terrain therefore shrink together and stay geometrically consistent.
- `1.0` is the level that matches the resolution the game is set to, and every
  match starts there.  Zooming never changes the resolution stored in the game
  options.
- The map area is clipped to the viewport at every level.  The whole-map level
  derives its factor from the map size at runtime
  (`min(map width / map area width, map height / map area height)`), so its map
  bounds rectangle has a different aspect ratio; it is fitted uniformly into the
  map area and letterboxed - the uncovered part is black, never stretched.
- Zoom is per-match view state: it starts at the native level when a scenario
  starts, when a saved game is loaded, on a campaign retry / next mission and on
  every other entry point that starts a match.  It is not saved and not
  networked.
- Only the three commands (plus the middle click) drive the zoom; the mouse wheel
  is deliberately not involved.

### Quick start

1. Copy `VentureLW.ini.example` to `VentureLW.ini` next to `gamemd.exe` (the
   sample already enables the module and lists all keys).
2. Keep `[Features] ViewZoom=yes` (it is also the default when the switch is
   missing) and `[VentureLW] ViewZoom.Enable=true`.
3. For the zoom-out direction, set `[VentureLW] ViewZoom.WideView=true` (it is
   off by default).
4. Start the game, open **Options -> Keyboard** and find the category
   **VentureLW**.  Bind the commands `Zoom In (Tactical View)`,
   `Zoom Out (Tactical View)` and `Reset Tactical View Zoom` to free keys - the
   platform does not hard-code any (suggested: keypad `+` / `-`, `Insert`).
5. Start a match and press the keys; `VentureLW.log` shows the level changes.

### Configuration

In `VentureLW.ini`:

```ini
[Features]
ViewZoom=yes

[VentureLW]
ViewZoom.Enable=true
ViewZoom.WideView=false
ViewZoom.WholeMap=true
ViewZoom.MiddleClickReset=true
ViewZoom.ShowHint=true
ViewZoom.Levels=0.25,0.33,0.5,0.67,1.0,1.25,1.5,2.0,2.5
ViewZoom.FullRedrawEvery=8
ViewZoom.MaxStaticSkip=4
ViewZoom.TranslateDirtyAreas=true
ViewZoom.SidebarNativeView=true
ViewZoom.BandFillOnScroll=false
ViewZoom.WideCellEnqueue=false
ViewZoom.NoBandFill=false
ViewZoom.DebugFill=false
ViewZoom.Diagnostics=false
```

The keys live in the **`[VentureLW]`** section of `VentureLW.ini`, not in
`[Features]` and not in `rules(md).ini`.  A key that is not written keeps its
default.  The values are read once during startup and the effective values are
logged; a value that cannot be parsed or is out of range falls back to the
default with a warning.

| Key (in `[VentureLW]`) | Type | Default | Range | Description |
| --- | --- | --- | --- | --- |
| `ViewZoom.Enable` | boolean | `true` | - | Master switch of the feature (the old key).  The module additionally needs `[Features] ViewZoom=yes`; both must be true.  `false` makes every hook replay the original instruction - vanilla behaviour. |
| `ViewZoom.WideView` | boolean | `false` | - | Enables the zoom-out direction (`k > 1`).  Zooming in works without it.  While it is `false` the zoom-out levels are unreachable and the zoom-out command is not registered at all. |
| `ViewZoom.WholeMap` | boolean | `true` | - | Adds the runtime whole-map level to the level table.  `false` removes the level; a `full` token in the table is then ignored with a warning. |
| `ViewZoom.MiddleClickReset` | boolean | `true` | - | A middle click anywhere in the game window returns to the native level; a press that moves more than 4 pixels is a drag and does not reset. |
| `ViewZoom.ShowHint` | boolean | `true` | - | Writes one line for every level change and reset to the log at INFO level; the per-match reset line is always written.  It does not put a message on screen. |
| `ViewZoom.Levels` | string (list of floating-point numbers and the keyword `full`) | `0.25,0.33,0.5,0.67,1.0,1.25,1.5,2.0,2.5` | at most 16 entries; 511 characters per INI value (platform INI limit) | The level table as map-resolution multipliers: `k < 1` zooms in, `k > 1` zooms out; `full` (also `whole` / `wholemap` / `all`) is the runtime whole-map level.  See "Level table design". |
| `ViewZoom.FullRedrawEvery` | integer | `8` | `-1..600` | Forces one whole-viewport engine repaint every N frames, which bounds the residue the engine's invalidation leaves behind while zoomed out.  `-1` disables the mechanism, `0` derives the interval from the measured cost of a forced frame. |
| `ViewZoom.MaxStaticSkip` | integer | `4` | `0..600` | While a zoomed-out level is active, a frame that changed nothing and did not move the camera skips the map resample entirely.  This caps how many frames in a row may be skipped (bounding a drawing path that changes pixels without reporting them).  `0` disables the skip. |
| `ViewZoom.TranslateDirtyAreas` | boolean | `true` | - | The renderer's invalidation inputs are expressed against the live viewport, which outside the render window is the native one; this moves the rectangles registered by the producers into the enlarged view.  A/B-tested as required - leave it on. |
| `ViewZoom.SidebarNativeView` | boolean | `true` | - | Presents the sidebar against the native viewport.  Turning it off brings back the old behaviour where the sidebar is placed past the right edge of the window at zoomed-out levels and stops updating - leave it on. |
| `ViewZoom.BandFillOnScroll` | boolean | `false` | - | Restarts the banded surface fill while dragging.  A/B-tested to cost more than it saves - leave it off. |
| `ViewZoom.WideCellEnqueue` | boolean | `false` | - | Enqueues cells of the enlarged view that are outside the window.  A/B-tested harmful - leave it off. |
| `ViewZoom.NoBandFill` | boolean | `false` | - | Skips the banded sweep that paints the enlarged surface for the first time.  A/B testing only. |
| `ViewZoom.DebugFill` | boolean | `false` | - | Paints the map area magenta instead of the frame, to see which surface is presented.  A/B testing only. |
| `ViewZoom.Diagnostics` | boolean | `false` | - | Writes a performance and geometry report to the log every few seconds (DEBUG level).  Performance investigation only. |

### Level table design

`ViewZoom.Levels` is the table of map-resolution multipliers, from the closest
view to the widest: `0.25` renders the map at a quarter of the game resolution
(4x magnification), `1.0` is the native level, `2.5` renders a 2.5x larger area
(a view 2.5x as wide).  The normalisation is fixed in code:

- the numeric levels are sorted ascending on load, so the order they are written
  in does not matter; "zoom in" always walks towards the front of the table;
- the native level is the numeric level closest to `1.0` - keep an exact `1.0` in
  the table, otherwise reset lands on e.g. `0.9` or `1.1`;
- at most 16 entries are kept; tokens past the limit are dropped and reported;
- fewer than two numeric levels - or an empty or unparsable list - falls back to
  the whole default table `0.25,0.33,0.5,0.67,1.0,1.25,1.5,2.0,2.5` (reported as
  `[ViewZoom] ViewZoom.Levels needs at least two numeric levels - using the
  default table`), so do not ship a single level;
- an invalid entry (`<= 0`, not a number) is skipped and reported as
  `[ViewZoom] ViewZoom.Levels: N token(s) are neither a positive factor nor a
  whole-map keyword and were ignored`;
- `full` (also accepted: `whole`, `wholemap`, `all`) is the runtime whole-map
  level.  When `ViewZoom.WholeMap=true` and the table is not already full
  (16 entries), the whole-map level is always the last level of the table: it is
  appended when the list does not name it, and an explicit `full` entry also ends
  up there and counts towards the 16-entry limit.  With
  `ViewZoom.WholeMap=false` the whole-map level does not exist and a `full` token
  is ignored with a warning;
- the whole-map level has no fixed factor (see the main description) and is the
  most expensive level: its first frame has to paint the whole enlarged surface,
  so entering it right after a match starts can hitch for a moment.  Every match
  starts at the native level for that reason.

Common starting points:

- **zoom in only** (cheapest, no zoom-out risk):
  `ViewZoom.Levels=0.25,0.33,0.5,0.67,1.0`, `ViewZoom.WideView=false`,
  `ViewZoom.WholeMap=false`;
- **zoom in plus conservative zoom-out**:
  `ViewZoom.Levels=0.25,0.33,0.5,0.67,1.0,1.25,1.5`, `ViewZoom.WideView=true`,
  `ViewZoom.WholeMap=false`;
- **the full default table plus the whole-map level**: keep the shipped defaults
  and set `ViewZoom.WideView=true`.

### Controls

The three commands are registered with the platform command registry (see
`docs/Commands.md`) and appear in **Options -> Keyboard** under the category
**VentureLW**.  The engine key does not carry a default binding; the player
assigns the keys there and the binding survives restarts.

| Engine command name (stored in the keyboard configuration) | What it does | Enabled when |
| --- | --- | --- |
| `Venture View Zoom In` | Moves one level towards `k < 1` (zoom in) | both switches are on |
| `Venture View Zoom Out` | Moves one level towards `k > 1` (zoom out); requires `ViewZoom.WideView=true` | both switches are on **and** `ViewZoom.WideView=true` |
| `Venture Reset View Zoom` | Returns to the native level | both switches are on |

- The UI names shown in the keyboard dialog are `Zoom In (Tactical View)`,
  `Zoom Out (Tactical View)` and `Reset Tactical View Zoom`; the engine names
  above carry the platform prefix and the keyboard configuration keys on them.
- A middle click anywhere in the game window does the same as the reset command
  while `ViewZoom.MiddleClickReset=true`; a press that moves more than 4 pixels
  is a drag and does not reset.
- With `ViewZoom.WideView=false` (the default) the zoom-out command is **not
  registered with the engine**, so it does not appear in Options -> Keyboard at
  all.  This differs from the fork, where the command is registered and pressing
  it only writes a hint to the log.
- Suggested bindings (free keys; the platform sets none): keypad `+` for zoom in,
  keypad `-` for zoom out, `Insert` for reset.

### Performance

Zooming out multiplies the number of cells the engine draws by `k²`, and a frame
is bounded by a single CPU core, so the zoomed-out levels trade frame time for
how much of the map is visible.  Two keys bound the cost:

- `ViewZoom.FullRedrawEvery` - the engine only repaints what its invalidation
  asks for, which leaves residue on screen while zoomed out.  Every N frames the
  platform forces one whole-viewport repaint, which caps that residue: `-1`
  disables the mechanism, `0` derives the interval from the measured cost of a
  forced frame, `8` is the value that measured well in game.  A smaller value
  means less residue and a higher average frame time; a larger value the
  opposite.
- `ViewZoom.MaxStaticSkip` - at a zoomed-out level, a frame that changed nothing
  and did not move the camera skips the map resample entirely, which is where
  most of the static-scene saving comes from.  This key caps how many frames in a
  row may be skipped, which bounds a drawing path that changes pixels without
  reporting them.  `0` disables the skip (every frame is resampled).

The single most effective performance setting is trimming the level table:
removing `2.0` and `2.5` removes the two most expensive levels.  Zooming in costs
no additional engine rendering, so a zoom-in-only configuration is cheap.

### When the configuration is read

The platform reads `VentureLW.ini` **once, when the game process starts** (the
feature modules declare and read their keys during platform startup).  Changing
any `ViewZoom.*` value therefore takes effect after **restarting the game**.
Starting a new match only resets the current level to the native one; it does not
re-read the INI.  (The fork hangs its reads on the options `LoadSettings` path
instead, so there a visit to Options or a restart is enough - the platform has no
such reload path.)  A value that cannot be parsed or is out of range is reported
as a warning at startup and the declared default is used; an unknown key in
`[VentureLW]` is reported too.

### Savegame

View zoom is per-match view state, like the camera position: **the feature makes
no savegame changes**.  It adds no fields to the platform's saved extension
state, a savegame written before installing it loads unchanged, and the current
zoom level is not stored - a loaded game starts at the native level again.

### Log

The module writes `[ViewZoom]` lines to `VentureLW.log`.  INFO lines: the startup
configuration summary, every level change, the reset command, the middle-click
reset and the per-match reset (always written, even with
`ViewZoom.ShowHint=false`).  DEBUG lines: geometry and surface diagnostics;
`ViewZoom.Diagnostics=true` adds a performance and geometry report every few
seconds (still DEBUG level, so `[Platform] LogLevel=debug` or
`VerboseLogging=yes` is needed to see it).  The `needs ViewZoom.WideView=true`
hint kept in the code is defensive only: with `ViewZoom.WideView=false` the
zoom-out command is not registered and the zoom-out levels are unreachable, so
the line does not occur in normal play.

At startup (excerpt; `ViewZoom.WideView=true`, other keys at their defaults):

```
[ViewZoom] enabled 1, middle click reset 1, hint 1, wide 1, whole map 1, debugfill 0, diagnostics 0, 10 levels
[ViewZoom] initialized (feature switch=1, ViewZoom.Enable=1, effective=1, 10 level(s))
[ViewZoom] commands: zoom in enabled (bound in Options -> Keyboard), zoom out enabled, reset enabled
```

In a match (the level-change lines keep the fork's wording and are gated by
`ViewZoom.ShowHint`):

```
[ViewZoom] zoom in: level 4/10 k=0.670 (native level 5 k=1.000)
[ViewZoom] zoom out: level 6/10 k=1.250 (native level 5 k=1.000)
[ViewZoom] middle click: level 5/10 k=1.000 (native level 5 k=1.000)
[ViewZoom] new match (scenario start): level 3 -> native 5
```

### Coexistence with another tactical-zoom platform

The closed-source host platform that implements the same tactical zoom
re-implements the entire feature - the tactical map zoomed independently of the
UI - so the conflict is **feature-level**: the two cannot be meaningfully
combined, and there is no single key or hook site whose removal would make them
coexist.  The platform therefore **does not detect** such a platform and does not
try to share or arbitrate the feature (deliberate decision, 2026-11-18).  With
both installed, run one of them: switch this module off with
`[Features] ViewZoom=no`, or leave the other platform's tactical zoom disabled.
Running both at the same time is unsupported, and the hook-level overlap with it
is not audited in this build.

### Differences from the Phobos-LWModified version

The 15 key names, their defaults, the level table, the algorithm and the three
actions are identical.  A configuration moved from the fork differs in the
following points:

| Aspect | Phobos-LWModified (fork) | VentureLW (platform) |
| --- | --- | --- |
| INI file and section | the mod's `[Phobos]` section (e.g. `RA2MD.ini` / `RA2MO.ini`) | the `[VentureLW]` section of `VentureLW.ini` (same key names) |
| Module switch | none - compiled in; only `ViewZoom.Enable` gates the feature | `[Features] ViewZoom=yes` (default on) **and** `ViewZoom.Enable=true` must both be true; with the module off every hook replays the original instruction |
| Read timing | hooked on the options `LoadSettings` path: opening the options screen or restarting reloads the keys | read once at game process start: a change requires restarting the game |
| Integer key validation | none | `ViewZoom.FullRedrawEvery` is validated to `-1..600` and `ViewZoom.MaxStaticSkip` to `0..600`; an out-of-range value falls back to the default with a warning |
| `ViewZoom.WideView=false` | all three commands are registered; pressing zoom-out writes a hint to the log and nothing happens | the zoom-out command is **not registered** (not visible in Options -> Keyboard); the other two are unaffected |
| Level table `full` | a latent bug: writing `full` explicitly dropped the whole-map level (the normalisation order lost the entry) | fixed: with `WholeMap=true` and fewer than 16 entries the whole-map level is always appended last, an explicit `full` included and counted in the 16-entry limit; with `WholeMap=false` a `full` token is ignored with a warning |
| Command names / category | engine names `View Zoom In` / `View Zoom Out` / `Reset View Zoom`, category "Interface" | engine names `Venture View Zoom In` / `Venture View Zoom Out` / `Venture Reset View Zoom`, category `VentureLW` - keyboard bindings from the fork are not reused, bind the new commands |
| Localisation | `TXT_VIEW_ZOOM_IN` / `_DESC`, `TXT_VIEW_ZOOM_OUT` / `_DESC`, `TXT_VIEW_ZOOM_RESET` / `_DESC` CSF keys | no CSF integration yet: the fork's `TXT_VIEW_ZOOM_*` keys have no effect here; the English UI strings are compiled into the DLL |
| Log | `debug.log` (developer log); `ShowHint` writes there | `VentureLW.log`, `[ViewZoom]` prefix, INFO/DEBUG split as described above |
| Savegame | no changes | no changes (same) |

### Implementation notes (platform)

- The module installs **22 engine hooks** - rendering (tactical passes, cell
  registration, dirty areas, surfaces), input (window messages, click
  coordinates, drag start/end, right-drag speed), view geometry and the two
  scenario entry points (`0x683AB0` scenario start / `0x67E440` load game) used
  for the per-match reset.  Addresses, sizes and handlers are listed in
  `docs/HookCatalogue.md`; every handler returns 0 when the module is off, so
  SyringeEx replays the original instruction.
- The keys are read in the module's `Initialize()` through the platform key
  registry (`VENTURE_INI_*`) and declared unconditionally, so the unknown-key
  scan and the template dump see them even when the module is switched off.
- The level-table parser is engine-free (`src/Features/ViewZoom/ViewZoomLevels.h`)
  and unit-tested without a game process.
- The feature keeps the fork's safety guards: a non-16-bit tactical surface or a
  failed large-surface allocation degrades to native behaviour with a log line
  instead of drawing wrong pixels.
- The feature writes no savegame state (see "Savegame").

### In-game checklist

- [ ] Startup `VentureLW.log` has the `[ViewZoom]` configuration summary, the
      effective values and no warning about an unknown or invalid
      `[VentureLW]` key.
- [ ] The commands appear in **Options -> Keyboard** under **VentureLW**, can be
      bound, and pressing them in a match changes the zoom (with
      `ViewZoom.WideView=false` only the zoom-in and reset commands appear).
- [ ] Zoom in through `0.67 -> 0.5 -> 0.33 -> 0.25`: the map magnifies while the
      sidebar, fonts and buttons keep their exact size and position.
- [ ] At a zoomed-in level, left-click selection and drag selection pick the unit
      under the cursor; edge scrolling, right-drag scrolling and the
      minimap / message bar / pause menu behave normally.
- [ ] The reset command and a middle click anywhere in the game window both
      return to the native level; a middle drag (more than 4 pixels) does not
      reset.
- [ ] Zoom out through `1.25 -> 1.5 -> 2.0 -> 2.5` (requires
      `ViewZoom.WideView=true`): the visible map area grows and the UI stays put;
      with `ViewZoom.WholeMap=true` the last level shows the whole map
      letterboxed (black outside the map, not stretched).
- [ ] Changing levels back and forth leaves no residue, black blocks or crash
      (`ViewZoom.FullRedrawEvery` bounds the residue; trimming the level table is
      the main performance knob).
- [ ] Every new match, campaign retry / next mission and loaded savegame starts
      at the native level.
- [ ] `[Features] ViewZoom=no` (restart): the game behaves as if the platform had
      no zoom - the commands are gone from Options -> Keyboard and the hooks
      replay the original instructions (vanilla A/B).
- [ ] Another platform that implements the same tactical zoom is installed as
      well: only one of the two features is switched on and the other is
      disabled - no visual corruption.
- [ ] Multiplayer: **not verified** for this feature; the zoom level is local
      per-match view state (no savegame, no simulation state).

> Ported from Phobos-LWModified commit `51fa3628` with the follow-up fixes
> `8c43bd3f` / `ff77ea04` / `65043b38` / `a88c5d48` / `b62f46ce` / `70c86561`
> (2026-10).  The 15 keys and the algorithm are unchanged; the section, the
> module switch, the command registration and the INI read timing follow the
> platform's rules.

---

# 中文说明

> 本节是上文的完整中文翻译，结构与英文部分一一对应；INI 键名、命令名、
> 日志原文保持原样。

## 战术视图缩放

- 战术地图按**自己的分辨率**单独渲染，因此地图可以独立放大/缩小，而
  **界面始终使用游戏设置的分辨率**：侧栏、字体、按钮、菜单的尺寸和位置
  一个像素都不随地图缩放。
- 放大（`k < 1`）保留原生视口，把它**居中的裁剪区**放大后铺满视口，
  不额外渲染引擎内容；缩小（`k > 1`）则**真的渲染更大的视口**——原本在视口外的
  格子也由引擎绘制——再把整幅画面缩回地图区。所以单位与地形**一起缩小、几何一致**。
- `1.0` 就是"与游戏设置分辨率一致"的那一档；每局都从这一档开始。
  缩放**不会写回**游戏设置里的分辨率。
- 每个档位下地图区都裁剪到视口范围内。最宽档的倍率在运行时按地图尺寸计算
  （`min(地图宽 / 地图区宽, 地图高 / 地图区高)`），因此它的地图边界矩形宽高比
  通常与地图区不同；它按等比适配放进地图区并加黑边——覆盖不到的地方填黑，
  **不是拉伸**。
- 缩放是**每局的瞬时视图状态**：开局、读档、战役重试 / 下一关以及其它所有
  开始一局的入口都会回到原生档。它不进存档，也不走网络。
- 只有三条命令（加中键单击）驱动缩放；**滚轮有意不参与**。

### 快速上手

1. 把 `VentureLW.ini.example` 复制到 `gamemd.exe` 旁边并改名为 `VentureLW.ini`
   （示例里已经打开模块并列出全部键）。
2. 保留 `[Features] ViewZoom=yes`（开关不写时也是默认开）与
   `[VentureLW] ViewZoom.Enable=true`。
3. 要使用缩小方向，设 `[VentureLW] ViewZoom.WideView=true`（默认关）。
4. 启动游戏，打开**选项 -> 键盘**，找到 **VentureLW** 分类，把
   `Zoom In (Tactical View)`、`Zoom Out (Tactical View)`、`Reset Tactical View Zoom`
   绑到空闲键上——平台不写死默认键（建议：小键盘 `+` / `-`、`Insert`）。
5. 进一局按这些键；`VentureLW.log` 会打印档位变化。

### 配置

在 `VentureLW.ini` 中：

```ini
[Features]
ViewZoom=yes

[VentureLW]
ViewZoom.Enable=true
ViewZoom.WideView=false
ViewZoom.WholeMap=true
ViewZoom.MiddleClickReset=true
ViewZoom.ShowHint=true
ViewZoom.Levels=0.25,0.33,0.5,0.67,1.0,1.25,1.5,2.0,2.5
ViewZoom.FullRedrawEvery=8
ViewZoom.MaxStaticSkip=4
ViewZoom.TranslateDirtyAreas=true
ViewZoom.SidebarNativeView=true
ViewZoom.BandFillOnScroll=false
ViewZoom.WideCellEnqueue=false
ViewZoom.NoBandFill=false
ViewZoom.DebugFill=false
ViewZoom.Diagnostics=false
```

这些键在 `VentureLW.ini` 的 **`[VentureLW]`** 段里，**不是** `[Features]` 段，
也不是 `rules(md).ini`。没写的键取默认值。值在启动时读取一次，生效值会写进日志；
无法解析或超出范围的值回落默认值并给 warning。

| 键（`[VentureLW]` 段） | 类型 | 默认 | 范围 | 说明 |
| --- | --- | --- | --- | --- |
| `ViewZoom.Enable` | 布尔 | `true` | - | 功能总开关（旧键）。模块还需要 `[Features] ViewZoom=yes`，两者都为真才动作。`false` 时所有 hook 回放被覆盖的指令——等同原版行为。 |
| `ViewZoom.WideView` | 布尔 | `false` | - | 缩小方向（`k > 1`）的总开关；不开也能放大。为 `false` 时缩小档不可达，且缩小命令**不注册**。 |
| `ViewZoom.WholeMap` | 布尔 | `true` | - | 是否把运行时最宽档加进档位表。`false` 时没有最宽档；此时表里的 `full` 词会被忽略并写 warning。 |
| `ViewZoom.MiddleClickReset` | 布尔 | `true` | - | 在游戏窗口内任意位置中键单击即回正（按下到抬起位移 > 4 像素视为拖动、不回正）。 |
| `ViewZoom.ShowHint` | 布尔 | `true` | - | 把切档与回正写进日志（INFO 级）；每局复位那一行总是写。**不会**在屏幕上弹消息。 |
| `ViewZoom.Levels` | 字符串（浮点数列表 + `full` 关键字） | `0.25,0.33,0.5,0.67,1.0,1.25,1.5,2.0,2.5` | 最多 16 项；平台 INI 单值上限 511 字符 | 地图分辨率倍率表：`k < 1` 放大、`k > 1` 缩小；`full`（也接受 `whole` / `wholemap` / `all`）代表运行时最宽档。详见"档位表设计"。 |
| `ViewZoom.FullRedrawEvery` | 整数 | `8` | `-1..600` | 每 N 帧强制一次整幅引擎重绘，给缩小档下引擎失效模型留下的残影封顶。`-1` 关闭该机制，`0` 按强制帧的实测成本自适应。 |
| `ViewZoom.MaxStaticSkip` | 整数 | `4` | `0..600` | 缩小档下，"这一帧没有任何变化且相机没动"就整帧跳过重采样。这个值封顶最多连续跳几帧，防住改了像素却不报脏的绘制路径。`0` 关闭跳过。 |
| `ViewZoom.TranslateDirtyAreas` | 布尔 | `true` | - | 渲染器登记的脏矩形是按活的视口算的，窗口外那是原生视口；这个键把生产者的矩形平移到放大视口。A/B 实测必需——保持打开。 |
| `ViewZoom.SidebarNativeView` | 布尔 | `true` | - | 侧栏按原生视口上屏。关掉 = 回到老行为：缩小档下侧栏被贴到窗口右边缘外并停止刷新——保持打开。 |
| `ViewZoom.BandFillOnScroll` | 布尔 | `false` | - | 拖动时也重启分带铺满。A/B 实测比省下的还贵——保持关闭。 |
| `ViewZoom.WideCellEnqueue` | 布尔 | `false` | - | 让窗口外的宽视口格子也入队。A/B 实测有害——保持关闭。 |
| `ViewZoom.NoBandFill` | 布尔 | `false` | - | 跳过首次把放大后的表面铺满的分带扫描。仅用于 A/B。 |
| `ViewZoom.DebugFill` | 布尔 | `false` | - | 用**品红**填地图区、不画帧，用来判断当前是哪块表面在上屏。仅用于 A/B。 |
| `ViewZoom.Diagnostics` | 布尔 | `false` | - | 每几秒往日志写一份性能 + 几何报告（DEBUG 级）。仅用于排查性能。 |

### 档位表设计

`ViewZoom.Levels` 是从"最近"到"最远"的地图分辨率倍率表：`0.25` = 地图按四分之一
分辨率渲染（画面放大 4 倍）；`1.0` = 原生；`2.5` = 渲染 2.5 倍大的区域（视野宽 2.5 倍）。
规范化逻辑在代码里固定：

- 数值档会在读入后**升序排序**，所以书写顺序无关；"放大"永远往表头走；
- **原生档 = 最接近 `1.0` 的数值档**——表里要保留精确的 `1.0`，否则回正会落到
  `0.9` / `1.1` 这种档上；
- 最多保留 **16 档**，超出部分丢弃并报告；
- 数值档不足两个（或表为空 / 解析不出数）→ **整表回退**到默认表
  `0.25,0.33,0.5,0.67,1.0,1.25,1.5,2.0,2.5`（日志会写
  `[ViewZoom] ViewZoom.Levels needs at least two numeric levels - using the
  default table`），所以不要只写一档；
- 非法项（`<= 0`、非数字）会被跳过并报告：
  `[ViewZoom] ViewZoom.Levels: N token(s) are neither a positive factor nor a
  whole-map keyword and were ignored`；
- `full`（也接受 `whole` / `wholemap` / `all`）是运行时最宽档。当
  `ViewZoom.WholeMap=true` 且表未满 16 档时，最宽档**永远是表尾那一档**：
  表里没写它时自动追加；显式写了 `full` 也排在表尾，并计入 16 档上限。
  `ViewZoom.WholeMap=false` 时最宽档不存在，表里的 `full` 词会被忽略并写 warning；
- 最宽档没有固定倍率（见开头说明），也是最贵的一档：它第一帧要把整个放大后的
  表面铺满，所以刚开局就切到最宽档会明显卡一下。每局都从原生档开始，原因就在这里。

常用组合：

- **只要放大**（最省，不承担缩小风险）：
  `ViewZoom.Levels=0.25,0.33,0.5,0.67,1.0`、`ViewZoom.WideView=false`、
  `ViewZoom.WholeMap=false`；
- **放大 + 保守缩小**：`ViewZoom.Levels=0.25,0.33,0.5,0.67,1.0,1.25,1.5`、
  `ViewZoom.WideView=true`、`ViewZoom.WholeMap=false`；
- **完整默认表 + 最宽档**：保留出厂默认值，设 `ViewZoom.WideView=true`。

### 操作

三条命令通过平台命令注册器注册（见 `docs/Commands.md`），在
**选项 -> 键盘** 的 **VentureLW** 分类里；引擎不设默认按键，由玩家在那里绑定，
绑定会随键盘配置保存、重启后仍然有效。

| 引擎命令名（键盘配置按它存） | 作用 | 启用条件 |
| --- | --- | --- |
| `Venture View Zoom In` | 往 `k < 1` 走一档（放大） | 双层开关都为真 |
| `Venture View Zoom Out` | 往 `k > 1` 走一档（缩小）；需要 `ViewZoom.WideView=true` | 双层开关都为真 **且** `ViewZoom.WideView=true` |
| `Venture Reset View Zoom` | 回到原生档 | 双层开关都为真 |

- 键盘对话框里显示的 UI 名是 `Zoom In (Tactical View)`、
  `Zoom Out (Tactical View)`、`Reset Tactical View Zoom`；上面的引擎名带平台前缀，
  键盘配置按引擎名保存。
- `ViewZoom.MiddleClickReset=true` 时，在游戏窗口内任意位置中键单击等同"回正"命令；
  按下到抬起位移 > 4 像素视为拖动、不回正。
- `ViewZoom.WideView=false`（默认）时，缩小命令**不向引擎注册**，
  在"选项 -> 键盘"里完全看不到。这与 fork 不同：fork 会注册该命令，按下只写一行提示日志。
- 建议绑定（空闲键；平台不设默认）：小键盘 `+` 放大、小键盘 `-` 缩小、
  `Insert` 回正。

### 性能

缩小会把引擎要画的格子数乘以 `k²`，而一帧受限于单个 CPU 核心；所以缩小档是在
"帧时间"与"能看到多少地图"之间做交换。两个键给开销封顶：

- `ViewZoom.FullRedrawEvery`——引擎只重画它认为失效的区域，缩小档下会留下残影。
  平台每 N 帧强制一次整幅视口重绘，给残影封顶：`-1` 关闭该机制，`0` 按强制帧的
  实测成本自适应，`8` 是实机拍板值。值越小残影越少、平均帧时间越高；越大则相反。
- `ViewZoom.MaxStaticSkip`——缩小档下，"这一帧没有任何变化且相机没动"就整帧跳过
  地图重采样，静止场景的节省主要来自这里。这个值封顶最多连续跳几帧，防住那些
  改了像素却不报脏的绘制路径。`0` 关闭跳过（每帧都重采样）。

性价比最高的性能调参是**裁档位表**：删掉 `2.0` 和 `2.5` 就删掉了最贵的两档。
放大方向不额外渲染引擎内容，所以"只要放大"的配置几乎不花钱。

### 配置读取时机

平台在**游戏进程启动时**读一次 `VentureLW.ini`（各功能模块在平台启动阶段声明并读取
自己的键）。因此改任何 `ViewZoom.*` 都要**重启游戏**才生效；开新对局只会把当前档位
复位到原生档，**不会**重读 INI。（fork 把读取挂在选项 `LoadSettings` 上，所以那边
开一次"选项"或重启即可；平台没有这条重载路径。）无法解析或超出范围的值在启动时
写 warning 并使用声明的默认值；`[VentureLW]` 段里的未知键也会被报告。

### 存档

视图缩放是每局的瞬时视图状态，类似相机位置：**本功能 0 存档变更**。它不给平台的
存档扩展状态增加任何字段，装它之前写的存档可以原样读取；当前档位也不进存档——
读档后回到原生档。

### 日志

模块把 `[ViewZoom]` 开头的行写进 `VentureLW.log`。INFO 级：启动配置摘要、每次切档、
回正命令、中键回正、每局复位（该行**总是**写，即使 `ViewZoom.ShowHint=false`）。
DEBUG 级：几何与表面诊断；`ViewZoom.Diagnostics=true` 会每几秒追加一份性能 + 几何
报告（仍是 DEBUG 级，需要 `[Platform] LogLevel=debug` 或 `VerboseLogging=yes` 才能看到）。
代码里保留的 `needs ViewZoom.WideView=true` 提示只是防御性日志：`ViewZoom.WideView=false`
时缩小命令不注册、缩小档不可达，正常玩法不会出现。

启动时（节选；`ViewZoom.WideView=true`，其余取默认值）：

```
[ViewZoom] enabled 1, middle click reset 1, hint 1, wide 1, whole map 1, debugfill 0, diagnostics 0, 10 levels
[ViewZoom] initialized (feature switch=1, ViewZoom.Enable=1, effective=1, 10 level(s))
[ViewZoom] commands: zoom in enabled (bound in Options -> Keyboard), zoom out enabled, reset enabled
```

对局中（切档行沿用 fork 的措辞，受 `ViewZoom.ShowHint` 控制）：

```
[ViewZoom] zoom in: level 4/10 k=0.670 (native level 5 k=1.000)
[ViewZoom] zoom out: level 6/10 k=1.250 (native level 5 k=1.000)
[ViewZoom] middle click: level 5/10 k=1.000 (native level 5 k=1.000)
[ViewZoom] new match (scenario start): level 3 -> native 5
```

### 与其它同功能战术缩放平台的共存

某个闭源宿主平台实现了同一个功能（战术地图与 UI 分辨率解耦地缩放），
冲突是**功能级**的：两者无法有意义地组合，也不存在删掉某个键或某个 hook 点就能
共存的做法。因此平台**不做检测**，也不尝试共享或仲裁该功能（2026-11-18 的决定）。
两个都装时只用其中一个：把本模块用 `[Features] ViewZoom=no` 关掉，或者关掉那个
平台的地图缩放。两者同时开不受支持；本轮也没有审计它与本模块的 hook 级重叠。

### 与 Phobos-LWModified（fork）版本的差异

15 个键名、默认值、档位表、算法和三个动作**完全相同**。从 fork 迁移配置时的差别：

| 方面 | Phobos-LWModified（fork） | VentureLW（平台） |
| --- | --- | --- |
| INI 文件与段 | MOD 的 `[Phobos]` 段（如 `RA2MD.ini` / `RA2MO.ini`） | `VentureLW.ini` 的 `[VentureLW]` 段（键名不变） |
| 模块开关 | 无——编译进去就常开，只有 `ViewZoom.Enable` 把关 | `[Features] ViewZoom=yes`（默认开）**与** `ViewZoom.Enable=true` 都为真才动作；模块关掉时所有 hook 回放被覆盖的指令 |
| 读取时机 | 挂在选项 `LoadSettings` 上：开一次"选项"或重启即可重载 | 游戏进程启动时读一次：改完必须**重启游戏** |
| 整数键校验 | 无 | `ViewZoom.FullRedrawEvery` 校验 `-1..600`、`ViewZoom.MaxStaticSkip` 校验 `0..600`；越界回落默认值并写 warning |
| `ViewZoom.WideView=false` | 三条命令都注册；按缩小只写一行提示日志，画面不动 | 缩小命令**不注册**（"选项 -> 键盘"里看不到）；另外两条不受影响 |
| 档位表 `full` | 潜伏 bug：显式写 `full` 反而会把最宽档丢掉（规范化顺序把该项弄丢了） | 已修正：`WholeMap=true` 且表未满 16 档时，最宽档总在表尾追加；显式 `full` 也生效并计入 16 档上限；`WholeMap=false` 时 `full` 词被忽略并写 warning |
| 命令名 / 分类 | 引擎名 `View Zoom In` / `View Zoom Out` / `Reset View Zoom`，分类 "Interface" | 引擎名 `Venture View Zoom In` / `Venture View Zoom Out` / `Venture Reset View Zoom`，分类 `VentureLW`——fork 的按键绑定不会沿用，需要重新绑定 |
| 本地化 | CSF 键 `TXT_VIEW_ZOOM_IN` / `_DESC`、`TXT_VIEW_ZOOM_OUT` / `_DESC`、`TXT_VIEW_ZOOM_RESET` / `_DESC` | 暂无 CSF 集成：fork 的 `TXT_VIEW_ZOOM_*` 键在这里**不生效**；UI 英文串编译在 DLL 里 |
| 日志 | `debug.log`（开发者日志）；`ShowHint` 写在那里 | `VentureLW.log`，`[ViewZoom]` 前缀，INFO/DEBUG 分级（见上） |
| 存档 | 0 变更 | 0 变更（相同） |

### 实现说明（平台）

- 模块安装 **22 个引擎 hook**——渲染（战术 pass、格子登记、脏区、表面）、输入
  （窗口消息、点击坐标、拖动开始/结束、右键拖动速度）、视图几何，以及用于每局复位的
  两个开局入口（`0x683AB0` scenario start / `0x67E440` load game）。地址、size 与
  handler 名单见 `docs/HookCatalogue.md`；模块关闭时每个 handler 都
  `return 0`，由 SyringeEx 回放被覆盖的指令。
- 键在模块的 `Initialize()` 里通过平台键注册表（`VENTURE_INI_*`）读取，并无条件声明，
  所以即使模块开关关掉，未知键扫描与模板导出也能看到它们。
- 档位表解析是 engine-free 的（`src/Features/ViewZoom/ViewZoomLevels.h`），
  有不需要游戏进程的单元测试。
- 功能保留 fork 的安全降级：战术表面不是 16 位、或大表面分配失败时，退回原生行为
  并写日志，而不是画出错误的像素。
- 功能不写任何存档状态（见"存档"）。

### 游戏内验证清单

- [ ] 启动后的 `VentureLW.log` 有 `[ViewZoom]` 配置摘要与生效值，且没有
      `[VentureLW]` 未知键/非法值的 warning。
- [ ] 命令出现在**选项 -> 键盘**的 **VentureLW** 分类里、能绑定，进对局按下去
      档位真的变化（`ViewZoom.WideView=false` 时只会出现放大与回正两条）。
- [ ] 逐档放大 `0.67 -> 0.5 -> 0.33 -> 0.25`：地图放大，侧栏、字体、按钮的尺寸和
      位置完全不变。
- [ ] 放大档下左键点选、框选落在光标下的单位上；边缘滚动、右键拖动、小地图 /
      消息栏 / 暂停菜单正常。
- [ ] 回正命令与窗口内任意位置的中键单击都回到原生档；中键拖动（超过 4 像素）不回正。
- [ ] 逐档缩小 `1.25 -> 1.5 -> 2.0 -> 2.5`（需要 `ViewZoom.WideView=true`）：
      可见地图范围变大、UI 不动；`ViewZoom.WholeMap=true` 时最后一档显示整张地图
      并带黑边（覆盖不到处填黑，不是拉伸）。
- [ ] 来回切档无残影、无黑块、无崩溃（`ViewZoom.FullRedrawEvery` 给残影封顶；
      性能主要靠裁档位表）。
- [ ] 新开一局、战役重试 / 下一关、读档后都从原生档开始。
- [ ] `[Features] ViewZoom=no`（重启）：游戏表现得像没装这个功能——命令从
      "选项 -> 键盘"消失，hook 回放原始指令（原版 A/B）。
- [ ] 同时装了另一个实现同款战术缩放的平台：两个功能开关只开一个、另一个关掉——
      无画面错乱。
- [ ] 联机：本功能**未验证**；档位是本地每局瞬时视图状态（不进存档、不涉及模拟状态）。

> 移植自 Phobos-LWModified（提交 `51fa3628`，后续修复 `8c43bd3f` / `ff77ea04` /
> `65043b38` / `a88c5d48` / `b62f46ce` / `70c86561`，2026-10）。
> 15 个键与算法未改动；段、模块开关、命令注册与 INI 读取时机按平台规则重做。
