#pragma once

// Feature vector x_i for a single order lifecycle (§ Feature Engineering)
struct FeatureRecord {
  double delta_t;         // \Delta t_i   — order age in nanoseconds
  double delta_imbalance; // \Delta I_i   — imbalance change over lifetime
  double size_ratio;      // size relative to best-level depth at placement
  double queue_pos;  // queue position (captured at add time, TODO: cancel-time)
  double dist_touch; // distance from same-side best price (raw price units)
  double cancel_rate; // local cancel rate in ±500ms window (TODO)
};
