# New or Enhanced Logics

## ROF curve (weapon-level spin-up)

- A weapon can fire faster - or slower - the longer it keeps firing.  This is
  the vanilla gattling idea moved from the unit onto the weapon: it is the same
  weapon from the first shot to the last, nothing is swapped and no staged
  weapon list is involved, so any weapon type can carry its own curve.
- Only the delay between two shots changes.  The projectile, the firing
  animation, the report, the warhead and the damage are untouched.
- The curve runs from `ROFCurve.Start` (progress `0`) to `ROFCurve.End`
  (progress `1`) over `ROFCurve.Ramp` frames.  Neither end has to be the slow
  one: `Start` is what the weapon fires at when the chain begins and `End` what
  it reaches at the top of the curve, so a weapon may just as well fire fastest
  first and wind down.  `ROFCurve.Power` shapes how it gets there: `1.0` is a
  straight line, above `1.0` stays near `Start` and then moves quickly, below
  `1.0` leaves `Start` immediately.
- `ROFCurve.Decay` brings the curve back down once the weapon stops firing: after
  that many frames without a shot the progress has fallen back to the start.
  `ROFCurve.Decay=0` resets it at once.
- `ROFCurve.Cycle` decides what reaching the top means: `no` holds the weapon at
  `End`, `yes` restarts the curve from `Start`.
- The curve is applied as a ratio to the reload delay the engine had already
  computed, so everything else that shapes the firing rate - the AttachEffect ROF
  multiplier, veterancy and `ROF.RandomDelay` - keeps working.
- Only the interval between two reloads is affected, so on a `Burst` weapon this
  is the interval between bursts, not the delay inside a burst.
- The spin-up state is tracked **per weapon**: a techno that carries several
  weapons keeps one curve for each of them, and firing one of them never touches
  another one's progress.  This is what a unit whose ammo logic falls back to
  another weapon needs - the fallback weapon's curve keeps building across the
  other weapon's shots instead of starting over, and a pause is settled by that
  weapon's own `ROFCurve.Decay`.  The chains are not saved: loading a save starts
  each curve over, which is what it looks like anyway.
- The module is opt-in per weapon: a weapon that does not write `ROFCurve=yes`
  and `ROFCurve.End` keeps its normal rate of fire, owns no chain and is not
  touched by the feature at all.  `[Features] ROFCurve` (default on) switches the
  module itself.

In `rulesmd.ini`:
```ini
[SOMEWEAPON]                 ; WeaponType
ROFCurve=no                  ; boolean
ROFCurve.Start=              ; integer, frames, default to the weapon's own ROF
ROFCurve.End=                ; integer, frames, required
ROFCurve.Ramp=30             ; integer, frames
ROFCurve.Power=1.0           ; floating point
ROFCurve.Decay=0             ; integer, frames
ROFCurve.Cycle=no            ; boolean
```

```{note}
- `ROFCurve.End` has to be set; without it the weapon is treated as not having a
  curve and keeps its normal rate of fire.
- `ROFCurve.Power` is clamped to `[0.1, 10.0]`, and a non-positive `Ramp` falls
  back to 30 frames; both corrections are reported in the platform log.
- A shot whose reload delay the engine computed as less than one frame is left
  alone rather than scaled.
- With `ROFCurve.Cycle=yes` the curve restarts as soon as it reaches the top, so
  the weapon drops back to the other end for one shot.
- `yes`/`no` (or `true`/`false`, `1`/`0`) are the accepted boolean spellings for
  the rules INI; `on`/`off` are read as "not written" by the engine's INI parser.
- A very large `End` combined with a large engine-computed delay saturates at the
  largest frame count instead of overflowing.
- The chain is kept per techno and per weapon type, never in the savegame, and
  the curve only reads the rules INI at load time - multiplayer sync is not
  affected by it.
```

### Differences from the Phobos-LWModified version

The seven keys, the curve, the formula and the per-weapon chain rules are
identical; the two releases differ in the following points (the old fork guide
still describes the fork's behaviour, so read the list when moving a
configuration over):

- **Switch**: the fork has no global switch (the feature is always on when
  compiled in).  The platform adds `[Features] ROFCurve` (default on).
- **Cooldown consumers**: the fork's curve also paces `SweepFire.Cooldown` and
  `Temporal.Interval`.  Those features are not ported to the platform yet
  (planned R17 / R19 / R20), so here the curve only affects the ordinary firing
  interval.
- **`DiskLaser=yes` weapons**: the fork's hook reads the weapon from a register
  that holds the `DiskLaserClass*` on that branch, so the curve silently never
  applied there.  The platform takes the weapon from `TechnoClass::Fire_At`'s
  own weapon-index argument and works on both branches.
- **Savegame**: the fork's seven key fields are part of its type-extension
  serialization; the platform keeps **zero savegame changes** (the keys are read
  from the rules INI at startup, the chains are never saved in either version).
- **Log**: the fork reports corrections through its developer log
  (`debug.log`); the platform writes `[ROFCurve] ...` lines to `VentureLW.log`.
- **Overflow guard**: the platform saturates a huge scaled delay instead of
  overflowing (behaviour identical for normal configurations).

### Implementation notes (platform)

- Injection points: the two `TechnoClass::Fire_At` reload sites - the common
  branch (`0x6FF2AA`, size 6) and the `DiskLaser=yes` branch (`0x6FE4B0`,
  size 6) - and the two `WeaponTypeClass::LoadFromINI` success epilogues
  (`0x7729B0` / `0x7729C7`, size 5, the same sites the official Phobos, Ares,
  Kratos and Mix use; the platform appends and returns 0).  See
  `docs/HookCatalogue.md`.
- The seven keys are read once, when the engine has parsed the weapon's section,
  and resolved into a configuration table keyed by the weapon type.  The table is
  feature-local on purpose: weapon types are process-lifetime objects, while the
  platform's extension stores are cleared on scenario clear (the savegame load
  path runs that clear too), so a type extension store would lose the curve
  configuration after every load.  Per-techno chains live in the shared
  `TechnoDataExt` (`src/Ext/TechnoDataExt.h`).
- The `DiskLaser=yes` branch is handled correctly: on that branch the register
  the fork used holds the `DiskLaserClass*` it just created, so the platform
  reads the weapon from the `Fire_At` weapon-index argument instead.
- Not migrated in this batch (they belong to features that do not exist in the
  platform yet): `SweepFire.Cooldown` scaling and `Temporal.Interval` scaling.
  The per-chain `LastRearm` field is already the interface they will use.

> Ported from Phobos-LWModified (commits `47023891` / `b950ac50` / `6c54bd6f`,
> 2026-10).  The seven keys and the curve are unchanged; the injection points and
> the state storage follow the platform's rules.

---

# 中文说明

> 本节是上文的完整中文翻译，结构与英文部分一一对应；INI 键名、函数/地址、
> 日志原文保持原样。

## ROF 曲线（武器级加速 / spin-up）

- 一把武器可以"越打越快"或"越打越慢"。这是把原版盖特的思路从**单位**搬到
  **武器**上：从第一发到最后一发始终是同一把武器，不换武器、不依赖武器阶列表，
  所以任何武器类型都能带自己的曲线。
- 只改变**两次开火之间**的装填间隔；抛射体、开火动画、音效、弹头、伤害一律不变。
- 曲线从 `ROFCurve.Start`（进度 `0`）走到 `ROFCurve.End`（进度 `1`），耗时
  `ROFCurve.Ramp` 帧。两端**哪端快由你定**：`Start` 是刚开始打时的射速、`End` 是
  曲线顶端；所以也可以"一开始最快、之后越来越慢"。`ROFCurve.Power` 决定过渡形状：
  `1.0` = 直线；大于 `1.0` = 先贴着 `Start` 不动、后段快速变化；小于 `1.0` = 一开打
  就明显离开 `Start`。
- `ROFCurve.Decay` 负责停火后的回落：停火这么多帧后进度退回起点。`Decay=0` 表示
  停火立刻归零。
- `ROFCurve.Cycle` 决定"到顶之后怎么办"：`no` = 停在 `End`；`yes` = 回到 `Start`
  重走一遍。
- 曲线是**按比例**套在引擎已经算好的装填时间上的，所以其它影响射速的修正
  （AttachEffect 的 ROF 倍率、老练度、`ROF.RandomDelay`）全部保留。
- 只影响两次装填之间的间隔；对 `Burst` 武器而言是"两轮之间"，不是一轮内部的连发间隔。
- 进度状态**按每把武器**分开记：一台单位带多把武器时每把各有一条曲线，一把武器开火
  不会动另一把的进度。这正是"弹药逻辑自动切到备用武器"的单位需要的——备用武器的曲线
  会跨过另一把武器开火的那几发继续积累、而不是每次从头来过；停顿则按**它自己**的
  `ROFCurve.Decay` 结算。链**不进存档**：读档后每条曲线从 0 开始（观感上本来也是这样）。
- 功能对**武器**是 opt-in：没写 `ROFCurve=yes` 与 `ROFCurve.End` 的武器保持原射速、
  不建链、完全不受影响。`[Features] ROFCurve`（默认开）是整个模块的开关。

在 `rulesmd.ini` 中：
```ini
[SOMEWEAPON]                 ; 武器类型（WeaponType）
ROFCurve=no                  ; 布尔
ROFCurve.Start=              ; 整数，帧；不写 = 武器自身 ROF
ROFCurve.End=                ; 整数，帧；**必填**
ROFCurve.Ramp=30             ; 整数，帧
ROFCurve.Power=1.0           ; 浮点数
ROFCurve.Decay=0             ; 整数，帧
ROFCurve.Cycle=no            ; 布尔
```

```{note}
- `ROFCurve.End` 必须写；不写就当作这把武器没有曲线、保持原射速。
- `ROFCurve.Power` 会被夹到 `[0.1, 10.0]`；`Ramp` 小于等于 0 时回退到 30 帧。
  两处修正都会写进平台日志。
- 引擎算出的装填小于 1 帧时保持原值、不缩放。
- `ROFCurve.Cycle=yes` 时一到达顶端就立刻从头开始，也就是那一发回到另一端。
- rules INI 里可接受的布尔写法是 `yes`/`no`（也接受 `true`/`false`、`1`/`0`）；
  `on`/`off` 会被引擎的 INI 解析器当作"没写"。
- `End` 极大、同时引擎装填也很大时，结果会**饱和**到最大帧数而不是溢出。
- 链是"每台单位、每把武器"各一份，不进存档；曲线只在载入 rules 时读取一次，
  不影响联机同步。
```

### 与 Phobos-LWModified（fork）版本的差异

7 个键、曲线、公式、每武器链的规则**完全相同**；两个发行版本的差别只在以下几点
（fork 的旧说明描述的仍是 fork 的行为，移植配置时对照此表）：

- **开关**：fork 没有全局开关（编译进去就常开）；平台新增 `[Features] ROFCurve`
  （默认开）。
- **冷却消费方**：fork 的曲线还会驱动 `SweepFire.Cooldown` 与 `Temporal.Interval`；
  这两个功能还没迁到平台（计划 R17 / R19 / R20），所以平台目前曲线只影响普通开火间隔。
- **`DiskLaser=yes` 武器**：fork 的 hook 在那个分支上读的寄存器里装的是
  `DiskLaserClass*`，导致曲线对这类武器**静默不生效**；平台改用
  `TechnoClass::Fire_At` 自己的武器槽参数取武器，两条分支都正确。
- **存档**：fork 的 7 个键字段随它的类型扩展进存档；平台保持 **0 存档变更**
  （键在启动时从 rules INI 读；两版的链都不进存档）。
- **日志**：fork 用开发者日志（`debug.log`）报告修正；平台把 `[ROFCurve] ...`
  行写进 `VentureLW.log`。
- **溢出保护**：平台会把超大的缩放结果饱和到最大帧数，而不是溢出
  （正常配置下行为一致）。

### 实现说明（平台）

- 注入点：`TechnoClass::Fire_At` 的两处装填写回点——通用分支（`0x6FF2AA`，size 6）
  与 `DiskLaser=yes` 分支（`0x6FE4B0`，size 6）——以及
  `WeaponTypeClass::LoadFromINI` 的两条成功返回路径（`0x7729B0` / `0x7729C7`，size 5；
  与官方 Phobos、Ares、Kratos、Mix 同址，平台串在后面并 `return 0`）。
  详见 `docs/HookCatalogue.md`。
- 7 个键在引擎解析完武器段之后读取一次，解析结果放进"按武器类型索引"的配置表。
  这张表是**功能本地**的：武器类型是进程级对象，而平台的扩展 store 会在 scenario clear
  （含读档路径）被清空，类型扩展 store 会在每次读档后丢掉曲线配置。
  每台单位的链放在共享的 `TechnoDataExt`（`src/Ext/TechnoDataExt.h`）里。
- `DiskLaser=yes` 分支被正确覆盖：fork 在那个分支上用的寄存器里装的是它刚创建的
  `DiskLaserClass*`，平台改为用 `Fire_At` 的武器槽参数取武器。
- 本轮未迁移（属于平台上还不存在的功能）：`SweepFire.Cooldown` 与
  `Temporal.Interval` 的缩放。链上的 `LastRearm` 字段已经是它们将来要用的接口。

> 移植自 Phobos-LWModified（提交 `47023891` / `b950ac50` / `6c54bd6f`，2026-10）。
> 7 个键与曲线未改动；注入点与状态存放按平台规则重做。

## SuddenDeath (warhead-level instant kill)

- A warhead can decide that whatever its detonation reaches dies, regardless of
  damage, armour, `Verses`, health, veterancy, `Immune=yes`, Iron Curtain or
  Force Shield.  This is the migrated fork feature, keyed by the warhead section
  exactly as before: `SuddenDeath=yes`.
- The hit is only **recorded** during the damage call; the kill happens at the
  head of the next logic frame.  That one-frame delay is deliberate: destroying
  an object from inside the detonation re-enters the object bookkeeping the
  engine is still walking (the fork lost a building animation to a second
  destruction that way).  It is invisible in play; only a trigger that watches
  the kill in the same frame would notice it.
- The same detonation sweeps the terrain inside `CellSpread + 0.5` cells: ore is
  cleared completely, bridges collapse through the engine's own IonCannon branch
  (so one hit always breaks the span, no `BridgeStrength` roll; both the
  high-bridge deck and the low-bridge overlay kind are covered), and terrain
  objects (trees, tiberium pillars) are removed without a health check - which is
  what makes the `Immune=yes` tiberium pillars of this mod removable.
- A unit that carries a SuddenDeath weapon gets the Attack cursor on an
  `Immune=yes` target and may fire at a target with a `Verses` entry of 0%: the
  engine's "cannot hurt it" gate and the cursor downgrade are bypassed for that
  weapon.  AI target selection is not changed; this is the manual fire path.
- `SuddenDeath.Mode=normal` (default) kills through the engine's ordinary death
  path: wreck, `DeathWeapon`, revenge weapon and kill credit all happen as usual.
  `SuddenDeath.Mode=erase` removes the object instead: no wreck, no
  `DeathWeapon`, no revenge weapon, no death animation, and no kill credit to the
  attacker (the kill is registered against the target's own owner).  A transport
  removed this way takes its passengers with it.
- The module is opt-in per warhead; `[Features] SuddenDeath` (default on)
  switches the module itself.  There are **no** platform INI keys: both keys live
  in the warhead section of `rules(md).ini`.

In `rulesmd.ini`:
```ini
[SOMEWARHEAD]                ; WarheadType
SuddenDeath=no               ; boolean, default no (opt-in)
SuddenDeath.Mode=normal      ; normal | erase, default normal
CellSpread=1.5               ; the area the terrain sweep covers, as usual
```

```{note}
- `SuddenDeath.Mode` only accepts the two words `normal` and `erase` (ASCII case
  insensitive).  A numeric value or any other word is rejected with a warning and
  the default `normal` stays in effect.
- `Damage=` does not decide whether the target dies - the execution is separate
  from the warhead's own damage.  It only decides which targets the engine's
  regular damage call reaches (and therefore which ones get recorded).
- Without `CellSpread` the sweep covers the impact cell only.
- Terrain objects still need Ctrl to be clicked unless the weapon has
  `TerrainFire=yes`; that is the vanilla `TreeTargeting` rule and is unrelated to
  this feature.
- The one-frame delay and the `erase` mode are the same as in the fork.  Unlike
  the fork, the platform keeps the two keys in a **type-level table that is never
  saved**: old and new savegames stay interchangeable, and switching the module
  on or off never changes a savegame.
- When the official Phobos is co-loaded and the target carries an active Phobos
  shield, Phobos' own handler at the armour-gate site returns early and skips the
  SuddenDeath bypass; the checked mod data configures no shields, and the
  documented fallback would be to move that hook one instruction earlier.
```

### Differences from the Phobos-LWModified version

- **No savegame change.**  The fork stored the two keys in its `WarheadTypeExt`,
  which made savegames from a build with the feature incompatible with one
  without it; the platform keeps a type-level table filled while `rules(md).ini`
  is parsed and never serializes anything.
- **No Phobos extension classes.**  The fork's erase mode called
  `TechnoExt::KillSelf(..., Vanish)` and its terrain removal called
  `TerrainTypeExt::Remove` / `PlayDestroyEffects`.  The platform reimplements both
  as pure YRpp virtual calls (the same engine sequence), so it works under
  Ares + official Phobos and without either.
- **`DestroyAnim` / `DestroySound` are not played.**  Those are Phobos TerrainType
  extension keys; the checked mod data does not use them, so the behaviour is
  identical with the shipped rules.  If they are ever needed, they are a separate
  small feature.
- **Bridge repair huts are covered.**  The shipped original-YR `[CABHUT]` is
  both `BridgeRepairHut=yes` and `Immune=yes`, and the engine returns from
  `BuildingClass::ReceiveDamage` for that combination before the platform's hit
  registration can see it; the ninth hook lets a SuddenDeath warhead through
  (the Mobile-Origin rules the fork was tested with are not `Immune`, so the
  fork never needed it).
- **Diagnostics are the platform log**, not a compile-time `constexpr` switch:
  `[SuddenDeath]` Info lines (configuration summary, the first kills, scenario
  clear), Debug lines (per hit, per execution, per swept cell) and Warn lines
  (invalid mode).  Set `[Platform] LogLevel=debug` to see the per-record trail.
- The keys, the modes and the cursor / fire-gate behaviour are unchanged, so a
  fork configuration migrates as-is (move the warhead sections into the new
  platform's `rules.ini`).

### Implementation notes (platform)

- Nine hooks (`docs/HookCatalogue.md`): `0x701900` records a hit at
  `TechnoClass::ReceiveDamage`; `0x75DEA0` reads the two keys in the
  `WarheadTypeClass::LoadFromINI` epilogue; `0x55B4E1` executes the queues at the
  head of `LogicClass::Update`; `0x6FCB64` lets a SuddenDeath weapon pass the
  armour/`Verses` gate; `0x417F63` / `0x51F179` / `0x74049A` keep the Attack
  cursor on an `Immune=yes` target for aircraft / infantry / units;
  `0x4423A3` lets such a warhead reach the ordinary damage path for a bridge
  repair hut (a building that is both `BridgeRepairHut=yes` and `Immune=yes`
  returns before `TechnoClass::ReceiveDamage` on its own; the next-frame
  execution marks its target so the neutral C4 warhead passes the same gate);
  `0x489286` records a
  detonation's centre and `CellSpread` at the entry of `MapClass::DamageArea`.
- The kill queue is `(target, attacker, house, mode)` with one entry per target
  per frame; the terrain queue is `(coords, CellSpread)`.  Both are transient and
  dropped by the platform's scenario-clear observer (`RegisterScenarioClearObserver`
  in `docs/EngineBindings.md`), so a savegame load, a retry or a return to the
  main menu can never inherit a pending kill.  The `RemovedTerrain` /
  `BlownBridges` de-duplication tables live in the same scenario scope.
- A pointer recorded in one frame is looked up in `TechnoClass::Array` before it
  is dereferenced in the next frame: an object that was deleted in between is
  simply skipped instead of being read.
- `erase` calls `KillPassengers`, `Stun`, `Limbo`, `RegisterKill(Owner)` and
  `UnInit` through the YRpp virtual slots (a building's bunker is unloaded
  first).  It never enters `ReceiveDamage`, which is exactly why no death effect
  happens.  The order matters and is documented in the source.
- The bridge sweep passes the engine's `IonCannonWarhead` to a one-point
  `MapClass::DamageArea` at the bridge deck height, then runs `BlowUpBridge` for
  the aftermath; a detonation produced by that nested call is ignored, so the
  sweep cannot feed itself.  A low bridge is a different cell kind: the call uses
  the cell's ground coords and there is no `BlowUpBridge`, because the engine's
  low-bridge branch removes the whole overlay run itself.
- Bridge **cell detection** is the one place where the pbsnew8 port was not
  enough: pbsnew8 triggers on `ContainsBridge() || ContainsBridgeEx()` (the
  bridge cell flags), and some original-YR maps leave those flags unset even
  though the engine destroys the bridge from a normal shell.  The sweep keeps
  that condition and also mirrors the engine's own test in
  `MapClass::DamageArea` (`0x489EC9`): the cell's `IsoTileTypeIndex` minus the
  bridge tileset base is compared with the `[General]` `BridgeMiddle1` /
  `BridgeMiddle2` tile groups (`BridgeTiles.h`).  Everything downstream is
  unchanged pbsnew8 behaviour.
- **Low bridges** (the `LOBRDG` / `LOBRDB` overlay families) are a second kind of
  bridge cell: an overlay on the water, with none of the bridge cell flags and
  none of the bridge tile types, so the two tests above never see them.  The
  engine destroys them from the same `MapClass::DamageArea` section, in a branch
  that compares the cell's `OverlayTypeIndex` with two hardcoded ranges
  (`0x48A217`: `74..99`, `0x48A26D`: `205..230`) and then hands the cell to its
  own run destroyer.  The sweep mirrors exactly those two ranges
  (`IsEngineLowBridgeOverlayIndex` in `BridgeTiles.h`) and uses the same
  `IonCannonWarhead` call.  The ranges are raw `[OverlayTypes]` indices, not
  names: on the shipped YR list `LOBRDG01` sits at 77 and is covered, while the
  family tail past slot 99 is outside the engine's own trigger for conventional
  weapons too, so the feature does not invent a path around it.
- No per-object extension, no savegame trailer row, no new platform INI key.
  The warhead table is a feature-local `unordered_map` filled when the rules are
  parsed (same pattern as the ROF curve).

## Universal scatter shaping (`LW.BallisticScatter.*`)

- The platform can take over the scatter of a projectile: the radius the host
  drew is remapped onto the projectile's own radius window (area-uniform by
  default), the final offset can be turned into a true, area-preserving
  ellipse, and the distance ramp is the platform's own.
  The keys are prefixed `LW.` because this is the platform's own system: the
  vanilla / host keys (`BallisticScatter.Min` / `.Max`, `Sigma`, `Aspect`,
  `Divergence`) are never read here and keep belonging to the host.
- It is **opt-in per projectile**.  A projectile section that contains any one
  of the `LW.BallisticScatter.*` keys is shaped; a projectile that writes none
  keeps the host behaviour bit for bit - its shots are left exactly as the host
  produced them and no other projectile is affected.  The module itself is
  `[Features] Scatter` (default on): with the switch off every hook returns
  immediately and no key is read.
- The shot only has a scatter to shape when the engine enters its own scatter
  block: `Inaccurate=yes` + `Arcing=yes` for the two `Fire_At` branches, and
  `FlakScatter=yes` + `Inviso=yes` for the classic `BulletClass::MoveTo` path.
  The keys on a projectile that never takes one of those paths do nothing.
- The radius is controlled by `Min` / `Max` (floating point cells, x256 =
  leptons, the radius at the weapon's full range), `Sigma` (radius-distribution
  exponent, default 0.5 = area-uniform), `Aspect` (area-preserving ellipse
  ratio, default 1 = no ellipse) and `Chance` (per-shot scatter probability,
  default 1).  The distance ramp is controlled by `Divergence`, whose fallback
  is the module-wide `[General] LW.Divergence` (default 0.5).  The
  `IncreaseByRange` family is read but **not applied** in this build, and
  `Physical` (B16) is a later commit of the round.  The burst-point scatter
  (B14/6A, see "The burst point" below) is in this build and uses the same keys.
- The platform draws **no random number of its own**.  `Chance` uses the host's
  own radius draw as the coin: the draw is uniform, so it passes with
  probability exactly `Chance`, and a passing draw is divided by `Chance` so it
  stays uniform on `[0, 1]` (the radius distribution of the scattering shots is
  unchanged).  A failing roll writes a zero radius: that shot goes to the aim
  point unscattered.
- The configuration is read once when `rules(md).ini` is parsed and never
  changes during a battle; it is not part of the savegame and the feature keeps
  no per-object state (see "Savegame and multiplayer" below).

In `rulesmd.ini`:
```ini
[SOMEPROJECTILE]                        ; ProjectileType
LW.BallisticScatter.Min=                ; floating point, cells (x256 = leptons), radius at full range; absent = host lower bound
LW.BallisticScatter.Max=                ; floating point, cells, radius at full range; absent = host upper bound
LW.BallisticScatter.Sigma=0.5           ; floating point, 0.5 .. 16; 0.5 = area-uniform, 1.0 = the engine's radial-uniform
LW.BallisticScatter.Aspect=1.0          ; floating point, 0.125 .. 8; area-preserving, >1 = longer across the flight line
LW.BallisticScatter.Divergence=         ; floating point, not below 0.05; absent = [General] LW.Divergence
LW.BallisticScatter.Chance=1.0          ; floating point, 0 .. 1; per-shot scatter probability
LW.BallisticScatter.IncreaseByRange=no  ; boolean; read but NOT applied in this build
LW.BallisticScatter.MinRange=           ; cells; read for validation only (the segmented interval is not applied yet)
LW.BallisticScatter.MaxRange=
LW.BallisticScatter.Min.InMinRange=
LW.BallisticScatter.Min.InMaxRange=
LW.BallisticScatter.Max.InMinRange=
LW.BallisticScatter.Max.InMaxRange=

[General]
LW.Divergence=0.5                       ; floating point, the global fallback of the per-projectile key
```

```{note}
- The gate is the presence of any one `LW.BallisticScatter.*` key in the
  projectile's section - it does not have to be `Min` or `Max`.  A section that
  writes only `LW.BallisticScatter.IncreaseByRange=no` is therefore shaped with
  all defaults.
- Values outside their documented range are clamped and reported with a
  `[Scatter]` warning (the warnings are capped per session); a non-finite number
  falls back to the default.  A `Min` greater than `Max` is swapped with a
  warning.
- With no `Min` / `Max`, the radius window is the host's own window for that
  path: visible `U[0, BallisticScatter]`, legacy
  `U[BallisticScatter / 2, BallisticScatter]` and classic
  `U[0, 2 * BallisticScatter]` at full range (`BallisticScatter` is the rules'
  `[CombatDamage] BallisticScatter`).  `Sigma=1` keeps the host's radial-uniform
  distribution; the default `Sigma=0.5` is the platform's area-uniform behaviour
  and is deliberately different.  On the classic path (whose engine
  `distance / range` factor the platform keeps), `Sigma=1` with no `Min` / `Max`
  reproduces the engine's radius bit for bit - the unit test pins that.
- The `Fire_At` distance ramp is clamped at the firing weapon's range: a shot
  beyond that range keeps the full-range radius.  The classic path keeps the
  engine's own unclamped linear `distance / range` factor instead.
- `LW.BallisticScatter.IncreaseByRange=yes` drops the projectile's whole
  configuration (with a warning) rather than giving it a half-implemented
  version of the fork's segmented interval - the projectile keeps the host
  scatter.  The six range keys are read for validation only.
- `Aspect` and `Divergence` only act at the `Fire_At` sites.  A projectile that
  only ever takes the classic path accepts them, but they do nothing there.
- Mixing `LW.BallisticScatter.*` with the host keys (`BallisticScatter.Min` /
  `.Max`, `Sigma`, `Aspect`, `Divergence`) in the same section drops the whole
  platform configuration with a warning: the platform normalizes against the
  host's default draw window, and a host key moves exactly that window, so
  shaping against it would be wrong.  The projectile then keeps the host
  scatter.  Remove the old keys when moving a projectile over.
- `Physical` (B16) is not in this build yet - the round's next commit adds it.
  The burst-point scatter is in this build; unlike the three shaping paths it
  needs both `Min` and `Max` (see "The burst point").
```

### The three engine paths

- **Visible `Fire_At` branch** (`Inaccurate` + `Arcing`, `FlakScatter && !Inviso`):
  the host draw is remapped at `0x6FE71C`, and the engine's own
  `draw * distance / GetWeaponRange` scaling is replaced at `0x6FE73F` by
  `shaped * (distance / range) ^ (1 / Divergence)`.  The `range` there is the
  firing weapon's own `WeaponTypeClass::Range`, not
  `TechnoClass::GetWeaponRange` - the latter can return the minimum range of the
  attached turret weapons (measured in the fork's real-machine test).
- **Legacy `Inaccurate` branch** (`Inaccurate` + `Arcing`,
  `!FlakScatter || Inviso`): the host draw is remapped at `0x6FE821`, then the
  platform adds the distance ramp the engine never had on this branch (with the
  same `Divergence` shape).
- **Classic `BulletClass::MoveTo` path** (`FlakScatter` + `Inviso`): the radius
  is shaped at `0x4687F8` only.  The engine's value is
  `RandomRanged(0, 2 * global) * distance / range`, so the platform normalizes
  the draw against that window and scales the shaped radius back by the same
  linear factor; `Divergence` is not stacked on top of it.  **This path gets no
  2-D ellipse**: its post-scatter sites are owned by Ares (`0x4688BD` always
  returns `0x468A3F`) and by the official Phobos / Mix obstacle handler
  (`0x4688A9` returns `0x4688BD` for Inviso bullets), so a chained handler there
  would never run.
- **A `FlakScatter` + `Inviso` projectile scatters twice**: the engine runs the
  `Fire_At` legacy branch first and then scatters again on the classic `MoveTo`
  path.  The platform shapes both radii, but the true ellipse only exists at the
  `Fire_At` exit, so the combined offset of such a bullet is an ellipse plus a
  plain circular offset, not a pure ellipse.  (The vanilla flak AA bullets are
  the projectiles of this kind; the fork avoids the blend by skipping the
  ellipse for this flag combination entirely.)
- **The true ellipse** is applied at the `Fire_At` scatter block's exit
  (`0x6FE8EE`), i.e. after both radius branches have produced the offset and
  before the facing / velocity / firing solution are derived from it.  It is
  area-preserving: with `Aspect > 1` the offset is `sqrt(Aspect)` longer across
  the flight line and `1 / sqrt(Aspect)` shorter along it (so `Aspect > 1` is
  longer across the shot, not along it).  The flight line is the shot's
  horizontal muzzle -> aim direction; a shot straight up or down has no flight
  line in the 2-D plane and is left circular.

### The burst point (ROT / Arm missiles, B14/6A)

- The engine steers a projectile with `ROT > 0` or `Arm > 0` (the vanilla
  guided-missile kinds) towards the centre of the live target on every frame, and
  neither of its own scatter paths reaches such a projectile (the `Fire_At` block
  needs `Inaccurate` + `Arcing`, the classic path `FlakScatter` + `Inviso`).
  Even a launch offset would be steered away again.  The platform therefore
  displaces the **burst point** instead.
- Gate: `Inaccurate=yes`, `ROT > 0` or `Arm > 0`, no `Arcing`, no `Inviso`, and
  **both** `LW.BallisticScatter.Min` and `.Max` written.  The explicit pair is
  required because this path has no host draw window to fall back on; a section
  that writes only one of the two keeps the engine's burst point (the platform
  logs a warning when that section is read).
- The offset reuses the same recipe: `Min` / `Max` at full range, `Sigma`
  (default 0.5), the `Divergence` ramp of the distance actually flown, `Chance`,
  and the area-preserving `Aspect` ellipse aligned with the chord from the burst
  point back to the launch point.  The base is the engine's own burst point, so a
  `Ranged` early detonation explodes where it happened (with the ramp of the
  shorter distance) instead of jumping onto the target.
- This is the **only** path of the module that draws random numbers: two draws on
  the detonation frame from the scenario's synchronised RNG (radius fraction,
  then angle fraction).  Both ends of a multiplayer match draw on the same frame
  and compute the same point; the battle's random-number stream differs from
  vanilla for the projectiles that use this path (a deliberate, documented
  difference - the fork behaves the same way).
- The projectile still flies to the target centre; only the warhead explodes
  off-centre.  A `Cluster>1` projectile keeps the engine's own cluster scatter
  layered on top of the displaced centre.  The offset does not apply to an
  `Arcing` / `Inviso` projectile (both already scatter) or without `Inaccurate`
  (the engine's snap code would overwrite the point again).

### Co-loads and the old key family

- Supported hosts: Ares + official Phobos.  Ares hooks `0x6FE709` / `0x6FE7FE`
  and returns into `0x6FE71C` / `0x6FE821`, i.e. it jumps **to** the platform's
  handlers: the platform reads the draw Ares produced in EAX and never draws or
  remaps it a second time.
- The official Phobos (and Mix) also hook the classic radius site `0x4687F8`
  and rewrite EAX before returning 0.  The platform is last in that chain, so it
  shapes the host's final radius.
- Mix's own handlers at `0x6FE71C` / `0x6FE821` are a different case: Mix
  reshapes the same EAX with its own old keys at the platform's own addresses.
  Co-loading Mix with this platform's scatter rows is an **unsupported scatter
  configuration** - both handlers write EAX and the last one in the chain wins.
- The host keys (`BallisticScatter.Min` / `.Max`, `Sigma`, `Aspect`,
  `Divergence`) and the platform keys must not be mixed in one projectile
  section.  Ares / the official Phobos read the host keys at the same sites, and
  a host key moves the draw window the platform normalizes against; the platform
  logs a warning and the resulting shaping is undefined-ish.  Remove the old
  keys when moving a projectile to `LW.BallisticScatter.*`.

### Savegame and multiplayer

- No savegame state: the configuration is a feature-local table filled when
  `rules(md).ini` is parsed, and nothing is serialized - old and new savegames
  stay interchangeable, and switching the module on or off never changes a
  savegame.
- The three shaping paths draw no random number of their own: `Chance` reuses the
  host's draw and every site only rewrites a value the host already produced, so
  for those paths the random-number stream is exactly the host's.  The
  burst-point path is the documented exception: it draws two numbers on the
  detonation frame from the synchronised RNG (both ends of a match draw on the
  same frame, so the point is identical; the stream differs from vanilla for the
  projectiles that use that path).
- A projectile that writes at least one `LW.` key is shaped and, with the
  platform defaults, deliberately **not** bit-identical to vanilla: the default
  `Sigma` is 0.5 (area-uniform) and the ramp defaults to the global `Divergence`
  0.5.  A projectile that writes no `LW.` key is exactly the host behaviour.
- Multiplayer was not verified yet for this round.

### Differences from the Phobos-LWModified version

The key names and the intent are the fork's; the platform implementation differs
in the following points on purpose:

- **`Sigma` default 0.5** (the fork's default is 1.0): the platform's default is
  area-uniform, the fork's is the engine-like radial-uniform (denser towards the
  centre).  Write `Sigma=1` to get the fork's default distribution back.
- **No extra random draws on the shaping paths**: the fork redraws the radius on
  the classic path and draws a coin for its `Chance`; the platform reuses the
  host's draw for both, so a co-loaded host's random-number stream (and the
  multiplayer synchronisation it carries) is untouched.  The burst-point path is
  the exception on both sides: it draws its two numbers explicitly (see "The
  burst point" above), exactly like the fork's B14/6A.
- **`Min` / `Max` mean the radius at full range on every path**: the fork keeps
  each engine path's own magnitude (its classic path doubles the values because
  the engine draws `U[0, 2 * global]` there); the platform normalizes every path
  onto the same full-range radius.
- **`IncreaseByRange` is read but not applied yet**: whether the fork's
  segmented interval is ported is decided after the in-game data; a projectile
  that writes it keeps the host scatter in this build.

### Implementation notes (platform)

- Eight hooks (`docs/HookCatalogue.md`): `0x46C41C/0xA` reads the projectile's
  keys in the `BulletTypeClass::LoadFromINI` success epilogue (ESI = the type,
  `CCINIClass*` at `[esp+0x90]`; the failure path is not hooked; shared with
  Ares / Kratos / official Phobos / Mix, all return 0); `0x6FE67D/6` captures a
  shot (projectile type, unscattered aim base, 3-D distance, firing weapon range,
  engine branch); `0x6FE71C/6` remaps the visible-branch draw (the post-Ares
  entry); `0x6FE73F/6` replaces the visible-branch distance scaling;
  `0x6FE821/6` remaps the legacy-branch draw and adds its ramp (the post-Ares
  entry); `0x6FE8EE/6` applies the true ellipse at the scatter block's exit;
  `0x4687F8/6` shapes the classic radius (last in the official Phobos / Mix /
  fork chain); `0x468DCF/6` rewrites the burst coordinate in
  `BulletClass::Explode` just before the `Inaccurate` branch jumps over the
  target snap (free in the audited co-load set - the fork uses `0x468FF4`,
  which the official Phobos / Mix / fork occupy with non-zero returns).
- `[General] LW.Divergence` is read through the platform's rules-INI
  notification (`0x6686C0`, `IniTakeover::RegisterRulesIniObserver`) when the
  engine has parsed `rules(md).ini` and before the projectile sections are read,
  so the per-projectile fallback is already resolved.  Every other key is read
  per projectile section at the INI site above, into a feature-local table keyed
  by the `BulletTypeClass` pointer; the table is filled once and only read
  during a battle.
- The geometry is engine-free and lives in
  `src/Features/Scatter/ScatterMath.h`, so the whole mapping is unit-tested
  without the game process (`tests/test_scatter_math.cpp`); the hooks only read
  the values they need and write the result back.
- Diagnostics are the platform log: `[Scatter]` Info lines (configuration and
  the resolved global divergence), Warn lines (invalid values, mixed host keys,
  the deferred `IncreaseByRange`, clamped values) and Debug lines for the shaped
  shots and unconfigured projectile types; the per-shot ones are capped per
  projectile type (and by a global session safety cap), so one test projectile
  cannot silence the other engine paths.
- All handlers return 0 (the trampoline replays the overridden instruction(s));
  a projectile without `LW.` keys leaves every register and the offset exactly
  as the host produced them.

## 即死弹头（弹头级秒杀）

- 弹头可以让它这一发爆炸覆盖到的一切直接死亡：伤害、护甲、`Verses`、血量、
  老练度、`Immune=yes`、铁幕、力场都不再是限制。这是从 fork 迁移过来的功能，
  弹头键完全沿用旧名：`SuddenDeath=yes`。
- 命中当帧**只登记**，处决发生在下一逻辑帧的开头。这一帧延迟是刻意的：在弹体
  引爆过程里销毁对象会重入引擎正在遍历的对象簿记（fork 曾因此把建筑动画二次
  销毁）。实战中看不出来；只有"在同一帧里检测死亡"的触发器会注意到这一帧之差。
- 同一次爆炸还会清扫 `CellSpread + 0.5` 范围内的地形：矿石**一次清光**，桥走
  引擎自己的离子炮分支（**一次必断**，不经过 `BridgeStrength` 概率；高架桥面与
  低桥覆盖物都算），地形对象（树、矿柱）**不做血量判定直接移除**——这正是
  `Immune=yes` 的矿柱也能被打掉的原因。
- 手里拿着即死弹头武器的单位，对 `Immune=yes` 目标**能出攻击光标**，对 `Verses`
  为 0% 的目标**能开火**：该武器的"打不动"判定与光标降级被绕过。这是玩家手动
  开火路径；AI 的选目标逻辑不受影响。
- `SuddenDeath.Mode=normal`（默认）走引擎正常死亡路径：留残骸、触发
  `DeathWeapon` 与复仇武器、正常记功。`SuddenDeath.Mode=erase` 则直接移除对象：
  无残骸、无死亡武器、无复仇武器、无死亡动画、**不计功**给攻击者（击杀记在目标
  自己的 owner 名下）。被这样抹掉的运输载具会连乘客一起消失。
- 行为按弹头逐段 opt-in；`[Features] SuddenDeath`（默认开）是模块总开关。
  **没有**平台侧 INI 键：两个键都在 `rules(md).ini` 的弹头段里。

在 `rulesmd.ini` 中：
```ini
[SOMEWARHEAD]                ; 弹头段（WarheadType）
SuddenDeath=no               ; 布尔，默认 no（opt-in）
SuddenDeath.Mode=normal      ; normal | erase，默认 normal
CellSpread=1.5               ; 地形清扫范围，按原版含义
```

```{note}
- `SuddenDeath.Mode` 只认 `normal` 与 `erase` 两个单词（ASCII 大小写不敏感）。
  写数字或别的词会按非法值告警，并保持默认 `normal`。
- `Damage=` 不决定目标死不死——处决与弹头自身伤害是两条路径。它只影响引擎那次
  常规伤害会打到谁，因而影响谁会**被登记**。
- 不写 `CellSpread` 时，清扫范围只有命中那一格。
- 地形对象仍然要按 Ctrl 才出攻击光标（除非武器写了 `TerrainFire=yes`），这是
  原版 `TreeTargeting` 规则，与本功能无关。
- 一帧延迟与 `erase` 语义与 fork 相同。但平台把两个键放在**从不入存档的类型级表**
  里：新旧存档可以互换，功能开关不影响任何存档内容。
- 与官方 Phobos 共载、且目标带着激活的 Phobos 护盾时，Phobos 在护甲门同址的
  handler 会提前返回，从而跳过即死旁路；已核对的 MOD 数据没有配置护盾，备选做法
  是把该 hook 前移一条指令（见 `docs/HookCatalogue.md` 该行备注）。
```

### 与 Phobos-LWModified（fork）版本的差异

- **0 存档变更。** fork 把两个键存进 `WarheadTypeExt`，导致带该功能的存档与不带
  的构建互不兼容；平台只在解析 `rules(md).ini` 时填一张类型级表，不序列化任何
  东西。
- **不依赖 Phobos 扩展类。** fork 的 erase 调 `TechnoExt::KillSelf(..., Vanish)`，
  地形移除调 `TerrainTypeExt::Remove` / `PlayDestroyEffects`；平台用纯 YRpp 虚调用
  复刻同一段引擎序列，因此在 Ares + 官方 Phobos 下和两者都不装时都能工作。
- **不播放 `DestroyAnim` / `DestroySound`。** 这两个是 Phobos 的 TerrainType 扩展
  键；已核对的本 MOD 数据没有使用，所以与共载官方 Phobos 时的实际行为一致。将来
  若需要，是独立的小功能。
- **桥梁维修小屋已覆盖。** 原版 YR 的 `[CABHUT]` 同时是
  `BridgeRepairHut=yes` 与 `Immune=yes`，引擎会在
  `BuildingClass::ReceiveDamage` 里对这一组合提前返回，平台的命中登记根本看不到它；
  第九个 hook 让即死武器跳过这段（fork 测试用的 MO 数据没有 `Immune=yes`，所以
  fork 没碰到这个问题）。
- **诊断走平台日志**，不是编译期 `constexpr` 开关：`[SuddenDeath]` 的 Info（配置
  摘要、前几次处决、场景清理）、Debug（逐条命中 / 处决 / 清扫格）、Warn（非法
  Mode）。把 `[Platform] LogLevel` 设为 `debug` 可看到逐条轨迹。
- 键名、两种模式、光标与开火门行为不变，fork 的配置可以直接迁移（把弹头段搬进
  新平台的 `rules.ini` 即可）。

### 实现说明（平台）

- 九个 hook（`docs/HookCatalogue.md`）：`0x701900` 在
  `TechnoClass::ReceiveDamage` 前门登记命中；`0x75DEA0` 在
  `WarheadTypeClass::LoadFromINI` 成功尾声读两个键；`0x55B4E1` 在
  `LogicClass::Update` 帧头执行队列；`0x6FCB64` 让即死武器通过护甲 / `Verses` 门；
  `0x417F63` / `0x51F179` / `0x74049A` 分别让飞机 / 步兵 / 载具对 `Immune=yes`
  目标保留攻击光标；`0x4423A3` 让即死武器绕过桥梁维修小屋的
  「`BridgeRepairHut=yes` + `Immune=yes` 提前返回」（下一帧处决用中性 C4 弹头，
  会再次撞上同一道门，所以处决时会把目标标记出来让这一次调用也放行）；`0x489286` 在
  `MapClass::DamageArea` 入口登记爆炸中心与 `CellSpread`。
- 击杀队列是 `(目标, 攻击者, 来源方, 模式)`，同一帧同一目标只留一条；地形队列是
  `(坐标, CellSpread)`。两者都是场景级瞬态，由平台的场景清理观察者
  （`RegisterScenarioClearObserver`，见 `docs/EngineBindings.md`）清空，所以读档、
  重试、回主菜单都不会继承上一局的待处决记录。`RemovedTerrain` / `BlownBridges`
  两张去重表也在同一生命周期内。
- 一帧登记的裸指针在下一帧使用前，先到 `TechnoClass::Array` 里确认它还在：期间
  已被删除的对象直接跳过，而不是解引用野指针。
- `erase` 通过 YRpp 虚槽依次调用 `KillPassengers`、`Stun`、`Limbo`、
  `RegisterKill(Owner)`、`UnInit`（建筑先 `UnloadBunker`）。它完全不进
  `ReceiveDamage`，这正是"没有任何死亡效果"的原因；顺序有讲究，写在源码注释里。
- 炸桥时以桥面高度向引擎的 `IonCannonWarhead` 请求一次 1 点伤害的
  `MapClass::DamageArea`，再调 `BlowUpBridge` 做善后；这次嵌套调用产生的爆炸会被
  忽略，因此清扫不会自我喂养。**低桥**是另一种桥格：坐标用格子地面高度，而且不调
  `BlowUpBridge`——引擎自己的低桥分支会把整段覆盖物一次清掉。
- **桥格识别**是 pbsnew8 移植唯一需要补逻辑的地方：pbsnew8 用
  `ContainsBridge() || ContainsBridgeEx()`（桥格标志），而部分原版 YR 地图的桥格
  不带这些标志，引擎却仍能用常规炮弹拆桥。平台保留该判据，并补上引擎
  `MapClass::DamageArea`（`0x489EC9`）自己的判据：格子的 `IsoTileTypeIndex` 减去
  桥瓦片基准后，与 `[General]` 的 `BridgeMiddle1` / `BridgeMiddle2` 瓦片组比较
  （`BridgeTiles.h`）。其后的 Z+416、IonCannon `DamageArea`、`BlowUpBridge` 全部
  沿用 pbsnew8。
- **低桥**（`LOBRDG` / `LOBRDB` 两族覆盖物）是第二种桥格：它铺在水面上的覆盖物，
  既没有桥格标志也没有桥瓦片，上面两条判据都看不到它。引擎在
  `MapClass::DamageArea` 的同一段里用另一条分支拆它：把格子的 `OverlayTypeIndex`
  与两段硬编码区间比较（`0x48A217`：`74..99`；`0x48A26D`：`205..230`），命中后
  交给引擎自己的整段拆除函数。平台原样镜像这两段区间
  （`BridgeTiles.h` 的 `IsEngineLowBridgeOverlayIndex`），走同一次
  `IonCannonWarhead` 调用。区间是 **`[OverlayTypes]` 的原始编号**，不是名字：原版
  YR 的 `LOBRDG01` 在 77、落在区间内；编号超过 99 的族尾，连同常规武器一样都在
  引擎自己的触发范围之外，本功能不为它另造路径。
- 没有 per-object 扩展、没有存档尾块、没有新增平台 INI 键。弹头表是 feature 私有
  的 `unordered_map`，在解析 rules 时填充（与 ROF 曲线同一模式）。

## 通用散布整形（`LW.BallisticScatter.*`）

- 平台可以接管某个抛射体的散布：宿主抽出的半径被重新映射到抛射体自己的半径窗口
  （默认按面积均匀分布），最终偏移可以整成面积等分的真椭圆，距离斜坡也改由平台
  计算。键名带 `LW.` 前缀，因为这是平台自建体系：原版 / 宿主的键
  （`BallisticScatter.Min` / `.Max`、`Sigma`、`Aspect`、`Divergence`）在这里
  一律不读，仍归宿主所有。
- 这是**按抛射体 opt-in** 的行为：抛射体自己的段里只要出现任意一个
  `LW.BallisticScatter.*` 键，该抛射体就被整形；一个键都不写的抛射体保持宿主的
  **逐位**行为——它的射击完全按宿主产出的结果保留，也不影响其它抛射体。模块本身
  由 `[Features] Scatter`（默认开）控制：关掉后所有 hook 立即返回、不读任何键。
- 只有引擎真正进入自己的散布块时才有散布可整形：两条 `Fire_At` 分支要求
  `Inaccurate=yes` + `Arcing=yes`，经典的 `BulletClass::MoveTo` 路径要求
  `FlakScatter=yes` + `Inviso=yes`。从不走这些路径的抛射体，写了键也不起作用。
- 半径由 `Min` / `Max`（浮点，单位格，x256 = leptons，含义是武器满射程处的半径）、
  `Sigma`（半径分布指数，默认 0.5 = 面积均匀）、`Aspect`（面积等分的椭圆比例，
  默认 1 = 不做椭圆）、`Chance`（单发散布概率，默认 1）控制。距离斜坡由
  `Divergence` 控制，缺省时取模块级 `[General] LW.Divergence`（默认 0.5）。
  `IncreaseByRange` 族在本构建**只读不生效**；`Physical`（B16）与爆发点散布是
  本轮后续提交，本构建不读。
- 平台**不抽任何随机数**。`Chance` 直接拿宿主自己的半径抽样当硬币：抽样在
  `[0, 1]` 上均匀，因此通过概率正好是 `Chance`；通过的样本再除以 `Chance`，仍
  均匀分布在 `[0, 1]` 上（会散布的那些发数的半径分布因此不变）。不通过时写 0
  半径：那一发不散布，直接飞向瞄准点。
- 配置在解析 `rules(md).ini` 时读取一次，战斗中不再变化；不进存档，功能本身也
  没有 per-object 状态（见下文"存档与联机"）。

在 `rulesmd.ini` 中：
```ini
[SOMEPROJECTILE]                        ; 抛射体类型（ProjectileType）
LW.BallisticScatter.Min=                ; 浮点，单位格（x256 = leptons），满射程处半径；不写 = 宿主下界
LW.BallisticScatter.Max=                ; 浮点，单位格，满射程处半径；不写 = 宿主上界
LW.BallisticScatter.Sigma=0.5           ; 浮点，0.5 .. 16；0.5 = 面积均匀，1.0 = 引擎的径向均匀
LW.BallisticScatter.Aspect=1.0          ; 浮点，0.125 .. 8；面积等分，>1 = 沿飞行线更"横"
LW.BallisticScatter.Divergence=         ; 浮点，不小于 0.05；不写 = [General] LW.Divergence
LW.BallisticScatter.Chance=1.0          ; 浮点，0 .. 1；单发散布概率
LW.BallisticScatter.IncreaseByRange=no  ; 布尔；本构建只读不生效
LW.BallisticScatter.MinRange=           ; 单位格；仅供校验（分段区间本构建不生效）
LW.BallisticScatter.MaxRange=
LW.BallisticScatter.Min.InMinRange=
LW.BallisticScatter.Min.InMaxRange=
LW.BallisticScatter.Max.InMinRange=
LW.BallisticScatter.Max.InMaxRange=

[General]
LW.Divergence=0.5                       ; 浮点，逐抛射体键的全局缺省值
```

```{note}
- 触发门是"抛射体段里出现任意一个 `LW.BallisticScatter.*` 键"——不必是 `Min` 或
  `Max`。因此只写 `LW.BallisticScatter.IncreaseByRange=no` 的段也会按全默认值被
  整形。
- 超出文档范围的值会被夹紧，并以 `[Scatter]` Warn 行上报（警告条数按会话封顶）；
  非有限数回退到默认值。`Min` 大于 `Max` 时会交换两者并告警。
- 不写 `Min` / `Max` 时，半径窗口就是该路径宿主的窗口：可见分支
  `U[0, BallisticScatter]`、旧分支 `U[BallisticScatter / 2, BallisticScatter]`、
  经典路径满射程处 `U[0, 2 * BallisticScatter]`（`BallisticScatter` 即 rules 的
  `[CombatDamage] BallisticScatter`）。`Sigma=1` 保持宿主的径向均匀分布；默认的
  `Sigma=0.5` 是平台声明的面积均匀行为，属于刻意不同。在经典路径上（平台保留
  引擎自己的 `distance / range` 因子），不写 `Min` / `Max` 且 `Sigma=1` 可以逐位
  复刻引擎半径——单元测试钉住了这一点。
- `Fire_At` 的距离斜坡在武器射程处封顶：超出射程的一发只保留满射程半径。经典
  路径则沿用引擎自己的、不封顶的线性 `distance / range` 因子。
- `LW.BallisticScatter.IncreaseByRange=yes` 会让该抛射体的整份配置作废（并告警），
  而不是给它一个"fork 分段区间的半成品"——该抛射体保持宿主散布。六个区间键仅供
  校验。
- `Aspect` 与 `Divergence` 只在 `Fire_At` 的站点上生效。只走经典路径的抛射体
  可以写这两个键，但不起作用。
- 同一段里把 `LW.BallisticScatter.*` 和宿主键（`BallisticScatter.Min` / `.Max`、
  `Sigma`、`Aspect`、`Divergence`）混写会让整份平台配置作废（并告警）：平台是拿
  宿主的默认抽样窗口做归一化的，而宿主键改的正是这个窗口，照它整形会算错；该
  抛射体保持宿主散布。迁移抛射体时请把旧键删掉。
- `Physical`（B16）本构建还没有，要等本轮下一提交；爆点散布本构建已有，但与三条
  整形路径不同，它要求 `Min` 与 `Max` 两个键都写（见"爆点散布"节）。
```

### 三条引擎路径

- **可见的 `Fire_At` 分支**（`Inaccurate` + `Arcing`，`FlakScatter && !Inviso`）：
  宿主抽样在 `0x6FE71C` 被重映射，引擎自己的 `draw * distance / GetWeaponRange`
  缩放则在 `0x6FE73F` 被替换为
  `shaped * (distance / range) ^ (1 / Divergence)`。这里的 `range` 取开火武器
  自己的 `WeaponTypeClass::Range`，不是 `TechnoClass::GetWeaponRange`——后者可能
  返回挂载炮塔武器的最小射程（fork 的实机测试实测到过这个问题）。
- **旧 `Inaccurate` 分支**（`Inaccurate` + `Arcing`，`!FlakScatter || Inviso`）：
  宿主抽样在 `0x6FE821` 被重映射，然后平台补上引擎在该分支上根本没有的距离斜坡
  （同样按 `Divergence` 成形）。
- **经典 `BulletClass::MoveTo` 路径**（`FlakScatter` + `Inviso`）：只在
  `0x4687F8` 整形半径。引擎给的值是
  `RandomRanged(0, 2 * global) * distance / range`，所以平台按这个窗口归一化抽样，
  再把整形后的半径乘回同一个线性因子；`Divergence` 不会叠加在它上面。**这条路径
  不做 2-D 椭圆**：它的散布后站点被 Ares（`0x4688BD` 总是返回 `0x468A3F`）和官方
  Phobos / Mix 的障碍物 handler（`0x4688A9` 对 `Inviso` 弹返回 `0x4688BD`）占据，
  串在那里的 handler 永远不会被执行。
- **`FlakScatter` + `Inviso` 的抛射体会散布两次**：引擎先走 `Fire_At` 旧分支，
  之后在经典 `MoveTo` 路径上再散布一次。平台把两处半径都整形了，但真椭圆只存在
  于 `Fire_At` 出口，所以这类子弹的合成偏移是"椭圆 + 一个普通圆偏移"，不是纯椭圆。
  （原版高射炮的 Flak AA 弹就属于这一类；fork 的做法是对这组 flag 直接跳过椭圆，
  以避免混合。）
- **真椭圆**在 `Fire_At` 散布块的出口（`0x6FE8EE`）施加，也就是两条半径分支都已
  产出偏移之后、朝向 / 速度 / 射击解由它推导之前。它是面积等分的：`Aspect > 1`
  时偏移沿飞行线的垂直方向被拉长 `sqrt(Aspect)` 倍、沿飞行线方向缩短
  `1 / sqrt(Aspect)` 倍（所以 `Aspect > 1` 是"横向更长"，不是"沿弹道更长"）。
  飞行线取该发水平方向的炮口 -> 瞄准点方向；垂直上 / 下射击在 2-D 平面里没有
  飞行线，保持圆形。

### 爆点散布（ROT / Arm 导弹，B14/6A）

- 引擎每帧把 `ROT > 0` 或 `Arm > 0` 的抛射体（原版自寻的 / 引信计时导弹）转向
  活体目标的中心，而且引擎自己的两条散布路径都进不去（`Fire_At` 块要求
  `Inaccurate` + `Arcing`，经典路径要求 `FlakScatter` + `Inviso`）；即使发射时
  抽出了偏移，制导也会把它带回目标中心。所以平台改为偏移**爆点**。
- 门控：`Inaccurate=yes`、`ROT > 0` 或 `Arm > 0`、没有 `Arcing`、没有 `Inviso`，
  并且 `LW.BallisticScatter.Min` 与 `.Max` **两个都写**。必须显式给一对边界，是
  因为这条路径没有宿主抽样窗口可以回退；只写一个键的段保持引擎爆点（平台在读取
  该段时会告警）。
- 偏移沿用同一套配方：满射程处的 `Min` / `Max`、`Sigma`（默认 0.5）、按**实际
  飞行距离**取 `Divergence` 斜坡、`Chance`，以及沿"爆点 -> 发射点"连线对齐的
  面积等分 `Aspect` 椭圆。基准点是引擎自己算出的爆点，所以 `Ranged` 提前引爆
  就在当时的位置炸（按较短距离的斜坡），不会瞬移到目标身上。
- 这是模块里**唯一**自己抽随机数的路径：引爆帧从场景的同步 RNG 抽两个数（先
  半径分数、再角度分数）。联机双端在同一帧抽样，算出的点一致；用了这条路径的
  弹种会让整局的随机数流与纯原版不同（刻意且有文档的差异，fork 同口径）。
- 弹体仍然飞向目标中心，只有战斗部偏着炸。`Cluster>1` 的弹保留引擎自己的集束
  散布，叠在已偏移的中心之上。`Arcing` / `Inviso` 的弹（两者本来就散布）与
  没有 `Inaccurate` 的弹不施加这个偏移（否则引擎的吸附代码会把爆点改回去）。

### 共载与旧键族

- 支持的宿主：Ares + 官方 Phobos。Ares 钩 `0x6FE709` / `0x6FE7FE` 并返回（跳）
  到 `0x6FE71C` / `0x6FE821`，即跳到平台的 handler 上：平台读的是 Ares 抽出的
  EAX，绝不重抽或二次重映射。
- 官方 Phobos（以及 Mix）也钩经典半径站点 `0x4687F8`，并在返回 0 前改写 EAX。
  平台排在这条链的最后，所以整形的是宿主给的最终半径。
- Mix 在 `0x6FE71C` / `0x6FE821` 上的处理是另一回事：Mix 用自己的一套旧键在平台
  的同一地址上重塑同一个 EAX。把 Mix 与平台这些散布行共载属于**不支持的散布
  配置**——两个 handler 都写 EAX，链上最后一个生效。
- 宿主键（`BallisticScatter.Min` / `.Max`、`Sigma`、`Aspect`、`Divergence`）与
  平台键**不能混写**在同一抛射体段里。Ares / 官方 Phobos 会在同一批站点读这些
  宿主键，而宿主键会改变平台归一化所用的抽样窗口，照它整形会算错；因此混写会让
  该抛射体的整份平台配置作废（并告警），该抛射体保持宿主散布。把抛射体迁到
  `LW.BallisticScatter.*` 时请删掉旧键。

### 存档与联机

- **0 存档状态**：配置是解析 `rules(md).ini` 时填充的功能私有表，不序列化任何
  东西——新旧存档可以互换，模块开关也不影响任何存档内容。
- **三条整形路径 0 额外随机抽样**：它们的站点都不调用 RNG；`Chance` 复用宿主
  抽样，其它站点只改写宿主已经产出的数值，因此这几条路径上随机数流与宿主完全
  一致。**爆点散布是文档化的例外**：它在引爆帧从同步 RNG 抽两个数（联机双端同帧
  抽样，点一致；用了这条路径的弹种会改变整局随机数流）。
- 写了至少一个 `LW.` 键的抛射体即被整形，在平台默认值下**按设计不**与纯原版
  逐位一致：默认 `Sigma` 是 0.5（面积均匀），距离斜坡默认取全局 `Divergence` 0.5。
  一个 `LW.` 键都不写的抛射体则与宿主行为完全一致。
- 联机本轮尚未验证。

### 与 Phobos-LWModified（fork）版本的差异

键名与意图沿用 fork 的；平台实现刻意在以下几点不同：

- **`Sigma` 默认 0.5**（fork 默认 1.0）：平台默认是面积均匀，fork 默认是类似引擎
  的径向均匀（中心更密）。想要 fork 的默认分布，写 `Sigma=1`。
- **三条整形路径不额外抽随机数**：fork 在经典路径上重抽半径，还会为 `Chance` 抽
  一次硬币；平台这两处都复用宿主抽样，所以共载宿主的随机数流（以及它所承载的
  联机同步）不受影响。**爆点散布在两边都是例外**：它自己抽两个数（见上文
  "爆点散布"），与 fork 的 B14/6A 相同。
- **`Min` / `Max` 在每条路径上都表示满射程处半径**：fork 保留各引擎路径自己的
  量级（它的经典路径因为引擎在那里抽 `U[0, 2 * global]` 而把数值翻倍）；平台把
  每条路径都归一到同一个满射程半径。
- **`IncreaseByRange` 只读、暂不生效**：fork 的分段区间是否移植，本轮要看实机
  数据再定；本构建里写了它的抛射体保持宿主散布。

### 实现说明（平台）

- 八个 hook（`docs/HookCatalogue.md`）：`0x46C41C/0xA` 在
  `BulletTypeClass::LoadFromINI` 成功尾声读抛射体的键（ESI = 类型，
  `CCINIClass*` 在 `[esp+0x90]`；失败路径不钩；与 Ares / Kratos / 官方 Phobos /
  Mix 同址，全部返回 0）；`0x6FE67D/6` 捕获一发（抛射体类型、未散布的瞄准基点、
  3-D 距离、开火武器射程、引擎分支）；`0x6FE71C/6` 重映射可见分支抽样（Ares 之后
  的入口）；`0x6FE73F/6` 替换可见分支的距离缩放；`0x6FE821/6` 重映射旧分支抽样并
  补距离斜坡（Ares 之后的入口）；`0x6FE8EE/6` 在散布块出口施加真椭圆；
  `0x4687F8/6` 整形经典路径半径（官方 Phobos / Mix / fork 链上的最后一个）；
  `0x468DCF/6` 在 `BulletClass::Explode` 里、`Inaccurate` 分支跳过目标吸附的
  `jne` 之前改写爆点坐标（在已审计的共载集里是空站点——fork 用的 `0x468FF4`
  被官方 Phobos / Mix / fork 的非 0 返回占住）。
- `[General] LW.Divergence` 走平台的 rules-INI 通知（`0x6686C0`，
  `IniTakeover::RegisterRulesIniObserver`）：引擎解析完 `rules(md).ini`、抛射体段
  被读取之前就会通知到，所以逐抛射体的缺省值已经就位。其余键都在上面的 INI 站点
  按抛射体段读取，放进一张以 `BulletTypeClass` 指针为键的功能私有表；表只在解析
  时填一次，战斗中只读。
- 几何部分不依赖引擎，放在 `src/Features/Scatter/ScatterMath.h`，所以整套映射不依赖
  游戏进程即可单元测试（`tests/test_scatter_math.cpp`）；hook 只负责读需要的数值并
  把结果写回。
- 诊断走平台日志：`[Scatter]` 的 Info 行（配置与解析出的全局 divergence）、Warn 行
  （非法值、混写宿主键、暂不生效的 `IncreaseByRange`、被夹紧的值），以及 Debug 行
  （逐发整形结果、未配置的抛射体类型）；逐发那类记录**按弹种**封顶（另有全会话
  总量保险），因此先测的那个弹种不会把其它引擎路径的日志挤掉。
- 所有 handler 都返回 0（trampoline 重放被覆盖的指令）；没有 `LW.` 键的抛射体，
  寄存器和偏移都保持宿主产出的原样。
