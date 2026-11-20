#pragma once

// ---------------------------------------------------------------------------
// Scatter core feature module (P1 / R10) - internal surface.
//
// The module implements the platform's own `LW.BallisticScatter.*` scatter
// system (design: the workspace's `VentureLW弹道与散布体系设计.md`, a document
// outside this repository).  It is built in three commits:
//
//   1. read-only host-sample probe  (this state): the engine hooks observe the
//      host's raw scatter draws and the final offsets and log them, so the
//      area-uniform ellipse mapping is derived from measured data instead of a
//      guess (the design document's §5.1/§5.3).  Nothing changes gameplay.
//   2. the real shaping: `LW.BallisticScatter.*` keys, radius remap, true
//      (area-uniform) ellipse and the divergence ramp.
//   3. the two follow-ups of the work package: burst-point scatter for the
//      engine-guided ROT/Arm missiles and the physical flight-path offset.
//
// The probe key (`[Scatter] Probe`) and the probe log lines are temporary: they
// are removed when the round is closed.
// ---------------------------------------------------------------------------

namespace venture::features::scatter
{
// The `[Features] Scatter` switch (default on).  When the module is off every
// hook returns immediately.
bool IsEnabled();

// Temporary R10 diagnostic: `[Scatter] Probe=yes` makes the hooks read-only and
// log one line per sampled shot.  The mapping commits drop this gate.
bool IsProbeEnabled();

// Whether Ares was detected at startup.  Ares 3.0 hooks 0x6FE709 / 0x6FE7FE and
// jumps straight into 0x6FE71C / 0x6FE821 with its own radius draw, so the
// sample has to be read against the right draw interval and the log records it.
bool HasAres();
} // namespace venture::features::scatter
