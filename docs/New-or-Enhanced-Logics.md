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
  (so one hit always breaks the span, no `BridgeStrength` roll), and terrain
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
  sweep cannot feed itself.
- Bridge **cell detection** is the one place where the pbsnew8 port was not
  enough: pbsnew8 triggers on `ContainsBridge() || ContainsBridgeEx()` (the
  bridge cell flags), and some original-YR maps leave those flags unset even
  though the engine destroys the bridge from a normal shell.  The sweep keeps
  that condition and also mirrors the engine's own test in
  `MapClass::DamageArea` (`0x489EC9`): the cell's `IsoTileTypeIndex` minus the
  bridge tileset base is compared with the `[General]` `BridgeMiddle1` /
  `BridgeMiddle2` tile groups (`BridgeTiles.h`).  Everything downstream is
  unchanged pbsnew8 behaviour.
- No per-object extension, no savegame trailer row, no new platform INI key.
  The warhead table is a feature-local `unordered_map` filled when the rules are
  parsed (same pattern as the ROF curve).

## 即死弹头（弹头级秒杀）

- 弹头可以让它这一发爆炸覆盖到的一切直接死亡：伤害、护甲、`Verses`、血量、
  老练度、`Immune=yes`、铁幕、力场都不再是限制。这是从 fork 迁移过来的功能，
  弹头键完全沿用旧名：`SuddenDeath=yes`。
- 命中当帧**只登记**，处决发生在下一逻辑帧的开头。这一帧延迟是刻意的：在弹体
  引爆过程里销毁对象会重入引擎正在遍历的对象簿记（fork 曾因此把建筑动画二次
  销毁）。实战中看不出来；只有"在同一帧里检测死亡"的触发器会注意到这一帧之差。
- 同一次爆炸还会清扫 `CellSpread + 0.5` 范围内的地形：矿石**一次清光**，桥走
  引擎自己的离子炮分支（**一次必断**，不经过 `BridgeStrength` 概率），地形对象
  （树、矿柱）**不做血量判定直接移除**——这正是 `Immune=yes` 的矿柱也能被打掉的
  原因。
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
  忽略，因此清扫不会自我喂养。
- **桥格识别**是 pbsnew8 移植唯一需要补逻辑的地方：pbsnew8 用
  `ContainsBridge() || ContainsBridgeEx()`（桥格标志），而部分原版 YR 地图的桥格
  不带这些标志，引擎却仍能用常规炮弹拆桥。平台保留该判据，并补上引擎
  `MapClass::DamageArea`（`0x489EC9`）自己的判据：格子的 `IsoTileTypeIndex` 减去
  桥瓦片基准后，与 `[General]` 的 `BridgeMiddle1` / `BridgeMiddle2` 瓦片组比较
  （`BridgeTiles.h`）。其后的 Z+416、IonCannon `DamageArea`、`BlowUpBridge` 全部
  沿用 pbsnew8。
- 没有 per-object 扩展、没有存档尾块、没有新增平台 INI 键。弹头表是 feature 私有
  的 `unordered_map`，在解析 rules 时填充（与 ROF 曲线同一模式）。
