#ifndef TIMING_H
#define TIMING_H

struct TimingStats {
  unsigned long sum;
  unsigned long maxVal;
  unsigned long count;
  unsigned long fails;

  void reset() { sum = 0; maxVal = 0; count = 0; fails = 0; }
  void record(unsigned long us) {
    sum += us;
    if (us > maxVal) maxVal = us;
    count++;
  }
  float avgMs() { return count > 0 ? (float)sum / count / 1000.0f : 0; }
  float maxMs() { return maxVal / 1000.0f; }
};

#endif
