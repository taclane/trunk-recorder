#include "fsk_offset_probe.h"

fsk_offset_probe::sptr fsk_offset_probe::make(float alpha) {
  return gnuradio::get_initial_sptr(new fsk_offset_probe(alpha));
}

fsk_offset_probe::fsk_offset_probe(float alpha)
    : gr::sync_block("fsk_offset_probe",
                     gr::io_signature::make(1, 1, sizeof(float)),
                     gr::io_signature::make(0, 0, 0)),
      d_alpha(alpha),
      d_hi(0),
      d_lo(0),
      d_offset(0),
      d_reset(true) {
}

int fsk_offset_probe::work(int noutput_items,
                           gr_vector_const_void_star &input_items,
                           gr_vector_void_star &output_items) {
  const float *in = (const float *)input_items[0];

  if (d_reset.exchange(false)) {
    d_hi = 0;
    d_lo = 0;
  }

  for (int i = 0; i < noutput_items; i++) {
    float mid = 0.5f * (d_hi + d_lo);
    if (in[i] > mid) {
      d_hi += d_alpha * (in[i] - d_hi);
    } else {
      d_lo += d_alpha * (in[i] - d_lo);
    }
  }

  d_offset = 0.5f * (d_hi + d_lo);
  return noutput_items;
}

float fsk_offset_probe::get_offset() const {
  return d_offset;
}

void fsk_offset_probe::reset() {
  d_offset = 0;
  d_reset = true;
}
