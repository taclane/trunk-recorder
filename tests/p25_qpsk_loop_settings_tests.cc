#include "trunk-recorder/systems/p25_qpsk_loop_settings.h"

#include <cassert>

int main() {
  constexpr P25QpskLoopSettings defaults;
  static_assert(defaults.gain_mu == P25QpskLoopSettings::default_gain_mu);
  static_assert(defaults.costas_alpha == P25QpskLoopSettings::default_costas_alpha);
  static_assert(defaults.valid());

  constexpr P25QpskLoopSettings narrower{0.0125, 0.004};
  static_assert(narrower.valid());

  assert(!(P25QpskLoopSettings{0.0, 0.004}.valid()));
  assert(!(P25QpskLoopSettings{0.0125, 0.0}.valid()));
  assert(!(P25QpskLoopSettings{1.01, 0.004}.valid()));
  assert(!(P25QpskLoopSettings{0.0125, 1.01}.valid()));

  return 0;
}
