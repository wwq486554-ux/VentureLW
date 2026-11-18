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
