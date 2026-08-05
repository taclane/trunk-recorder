#ifndef P25_QPSK_LOOP_SETTINGS_H
#define P25_QPSK_LOOP_SETTINGS_H

struct P25QpskLoopSettings {
  static constexpr double default_gain_mu = 0.025;
  static constexpr double default_costas_alpha = 0.008;

  double gain_mu = default_gain_mu;
  double costas_alpha = default_costas_alpha;

  constexpr bool valid() const {
    return gain_mu > 0.0 && gain_mu <= 1.0 &&
           costas_alpha > 0.0 && costas_alpha <= 1.0;
  }
};

#endif // P25_QPSK_LOOP_SETTINGS_H
