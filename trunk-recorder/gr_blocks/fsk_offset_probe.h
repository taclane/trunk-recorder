#ifndef FSK_OFFSET_PROBE_H
#define FSK_OFFSET_PROBE_H

#include <atomic>
#include <gnuradio/io_signature.h>
#include <gnuradio/sync_block.h>

// Estimates the carrier offset of a 2-level FSK signal from its frequency
// discriminator output (rad/sample). The upper and lower FSK levels are
// tracked separately and the offset is their midpoint, so the estimate isn't
// biased by an uneven mix of ones and zeros in the data.
class fsk_offset_probe : public gr::sync_block {
public:
#if GNURADIO_VERSION < 0x030900
  typedef boost::shared_ptr<fsk_offset_probe> sptr;
#else
  typedef std::shared_ptr<fsk_offset_probe> sptr;
#endif

  static sptr make(float alpha);
  fsk_offset_probe(float alpha);

  int work(int noutput_items,
           gr_vector_const_void_star &input_items,
           gr_vector_void_star &output_items);

  float get_offset() const; // rad/sample
  void reset();

private:
  float d_alpha;
  float d_hi;
  float d_lo;
  std::atomic<float> d_offset;
  std::atomic<bool> d_reset;
};

#endif
