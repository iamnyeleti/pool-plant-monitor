// vib_history.h
// Header-only. No Arduino dependency, so the same code can be tested on a laptop.
//
// Contains:
//   1. windowStd()        spread of the vibration signal over the sample window
//   2. StoppedDetector    decides if the pump has stopped while it should be running
//   3. MinuteRecorder     one record per minute, 24 hours, held in RAM only
//   4. Summary / advise() counts over a window and a simple rule-based advice code
//
// Every number marked PLACEHOLDER is a value chosen by the author of this code.
// None of them has been validated against a real pump or a standard.

#ifndef VIB_HISTORY_H
#define VIB_HISTORY_H

#include <stdint.h>
#include <math.h>

namespace vh {

// ------------------------------------------------------------------
// PLACEHOLDER parameters (change here only)
// ------------------------------------------------------------------
static const float    ELEV_FRAC_OF_MINUTE = 0.20f;  // a minute counts as elevated if 20% or more of its samples were CAUTION or WARNING
static const float    STOP_FRAC_OF_MINUTE = 0.50f;  // a minute counts as stopped if half or more of its samples were STOPPED
static const float    OFF_FRAC_OF_MINUTE  = 0.50f;  // a minute counts as off schedule if half or more of its samples were off schedule

static const uint16_t MIN_MONITORED_MIN   = 60;     // below this many monitored minutes, give no advice
static const uint16_t STOPPED_MIN_LIMIT   = 5;      // this many stopped minutes triggers the stopped advice
static const float    SHARE_LIMIT         = 0.25f;  // elevated for 25% or more of monitored time
static const uint16_t EPISODE_LIMIT       = 3;      // this many separate elevated episodes counts as repeated
static const float    EPISODE_SHARE_MIN   = 0.05f;  // and at least 5% of monitored time elevated
static const float    GAP_LIMIT           = 0.20f;  // more than 20% of minutes missing counts as data gaps
static const uint16_t EPISODE_MERGE_GAP_MIN = 10;   // elevated minutes closer together than this belong to the same episode

// ------------------------------------------------------------------
// 1. Spread of the signal
// ------------------------------------------------------------------
// Standard deviation of the values in buf[0..count). Removes any constant offset,
// so a sensor sitting still reads close to zero whatever its scale error or orientation.
inline float windowStd(const float* buf, int count) {
  if (count < 2) return 0.0f;
  float mean = 0.0f;
  for (int i = 0; i < count; i++) mean += buf[i];
  mean /= count;
  float ss = 0.0f;
  for (int i = 0; i < count; i++) {
    float d = buf[i] - mean;
    ss += d * d;
  }
  return sqrtf(ss / count);
}

// ------------------------------------------------------------------
// 2. STOPPED detector
// ------------------------------------------------------------------
// STOPPED is declared only when the pump is expected to be running AND the signal
// spread has stayed below the threshold for the whole hold time.
// To leave STOPPED the spread must rise above threshold * exitFactor (stops flickering).
class StoppedDetector {
 public:
  StoppedDetector(float stdThreshold, uint32_t holdMs, float exitFactor)
      : thr_(stdThreshold), hold_(holdMs), exitFactor_(exitFactor),
        below_(false), stopped_(false), belowSince_(0) {}

  void reset() { below_ = false; stopped_ = false; }

  bool update(uint32_t nowMs, float dynStd, bool expectedRunning, bool windowFull) {
    if (!expectedRunning || !windowFull) {
      reset();
      return false;
    }
    if (stopped_) {
      if (dynStd > thr_ * exitFactor_) reset();
      return stopped_;
    }
    if (dynStd < thr_) {
      if (!below_) {
        below_ = true;
        belowSince_ = nowMs;
      }
      if ((uint32_t)(nowMs - belowSince_) >= hold_) stopped_ = true;
    } else {
      below_ = false;
    }
    return stopped_;
  }

  bool stopped() const { return stopped_; }

 private:
  float thr_;
  uint32_t hold_;
  float exitFactor_;
  bool below_;
  bool stopped_;
  uint32_t belowSince_;
};

// ------------------------------------------------------------------
// 3. One record per minute
// ------------------------------------------------------------------
enum Flags : uint8_t {
  F_VALID    = 1,   // the minute had at least one sample
  F_CAUTION  = 2,   // elevated (CAUTION or WARNING) for ELEV_FRAC_OF_MINUTE or more of the minute
  F_WARNING  = 4,   // WARNING for ELEV_FRAC_OF_MINUTE or more of the minute
  F_STOPPED  = 8,   // STOPPED for STOP_FRAC_OF_MINUTE or more of the minute
  F_OFFSCHED = 16   // off schedule for OFF_FRAC_OF_MINUTE or more of the minute
};

struct MinuteRecord {
  float mean;      // mean RMS over the minute
  float maxv;      // highest RMS in the minute
  uint8_t flags;
};

struct Summary {
  uint16_t windowMin;     // minutes examined
  uint16_t validMin;      // minutes that had data
  uint16_t monitoredMin;  // valid minutes that were not off schedule
  uint16_t elevatedMin;   // monitored minutes flagged CAUTION or WARNING
  uint16_t warningMin;    // monitored minutes flagged WARNING
  uint16_t stoppedMin;    // monitored minutes flagged STOPPED
  uint16_t episodes;      // separate elevated episodes (runs closer than EPISODE_MERGE_GAP_MIN are merged)
  float    elevatedShare; // elevatedMin / monitoredMin
};

class MinuteRecorder {
 public:
  static const int N = 1440;  // 24 hours of minutes

  MinuteRecorder() : head_(0), count_(0), started_(false), minuteStart_(0) { clearAccum_(); }

  // Call once per loop with the current RMS and the sample flags
  // (any of F_CAUTION, F_WARNING, F_STOPPED, F_OFFSCHED; F_WARNING also counts as elevated).
  void update(uint32_t nowMs, float rms, uint8_t sampleFlags) {
    if (!started_) {
      started_ = true;
      minuteStart_ = nowMs;
    }
    int guard = 0;
    while ((uint32_t)(nowMs - minuteStart_) >= 60000UL) {
      finalize_();
      minuteStart_ += 60000UL;
      if (++guard > N) {            // enormous gap, do not loop for ever
        minuteStart_ = nowMs;
        break;
      }
    }
    sum_ += rms;
    n_++;
    if (rms > max_) max_ = rms;
    if (sampleFlags & (F_CAUTION | F_WARNING)) nElev_++;
    if (sampleFlags & F_WARNING) nWarn_++;
    if (sampleFlags & F_STOPPED) nStop_++;
    if (sampleFlags & F_OFFSCHED) nOff_++;
  }

  int count() const { return count_; }

  // minutesAgo = 0 is the most recent completed minute
  bool get(int minutesAgo, MinuteRecord& out) const {
    if (minutesAgo < 0 || minutesAgo >= count_) return false;
    int idx = head_ - 1 - minutesAgo;
    while (idx < 0) idx += N;
    out = rec_[idx];
    return true;
  }

  Summary summarise(int windowMinutes) const {
    Summary s = {0, 0, 0, 0, 0, 0, 0, 0.0f};
    int w = windowMinutes;
    if (w > count_) w = count_;
    uint16_t sinceElevated = EPISODE_MERGE_GAP_MIN;   // minutes since the last elevated minute
    for (int i = w - 1; i >= 0; i--) {          // oldest to newest
      MinuteRecord r;
      get(i, r);
      s.windowMin++;
      if (!(r.flags & F_VALID)) { if (sinceElevated < 60000) sinceElevated++; continue; }
      s.validMin++;
      if (r.flags & F_OFFSCHED) { if (sinceElevated < 60000) sinceElevated++; continue; }
      s.monitoredMin++;
      bool elevated = (r.flags & (F_CAUTION | F_WARNING)) != 0;
      if (elevated) {
        s.elevatedMin++;
        if (sinceElevated >= EPISODE_MERGE_GAP_MIN) s.episodes++;
        sinceElevated = 0;
      } else if (sinceElevated < 60000) {
        sinceElevated++;
      }
      if (r.flags & F_WARNING) s.warningMin++;
      if (r.flags & F_STOPPED) s.stoppedMin++;
    }
    s.elevatedShare = s.monitoredMin ? (float)s.elevatedMin / (float)s.monitoredMin : 0.0f;
    return s;
  }

 private:
  void clearAccum_() {
    sum_ = 0.0f; max_ = 0.0f;
    n_ = nElev_ = nWarn_ = nStop_ = nOff_ = 0;
  }

  void finalize_() {
    MinuteRecord r;
    if (n_ == 0) {
      r.mean = 0.0f; r.maxv = 0.0f; r.flags = 0;
    } else {
      float n = (float)n_;
      r.mean = sum_ / n;
      r.maxv = max_;
      uint8_t f = F_VALID;
      if ((float)nElev_ / n >= ELEV_FRAC_OF_MINUTE) f |= F_CAUTION;
      if ((float)nWarn_ / n >= ELEV_FRAC_OF_MINUTE) f |= F_WARNING;
      if ((float)nStop_ / n >= STOP_FRAC_OF_MINUTE) f |= F_STOPPED;
      if ((float)nOff_  / n >= OFF_FRAC_OF_MINUTE)  f |= F_OFFSCHED;
      r.flags = f;
    }
    rec_[head_] = r;
    head_ = (head_ + 1) % N;
    if (count_ < N) count_++;
    clearAccum_();
  }

  MinuteRecord rec_[N];
  int head_;
  int count_;
  bool started_;
  uint32_t minuteStart_;
  float sum_, max_;
  uint32_t n_, nElev_, nWarn_, nStop_, nOff_;
};

// ------------------------------------------------------------------
// 4. Advice code (rules are PLACEHOLDERS, first match wins)
// ------------------------------------------------------------------
enum Advice : uint8_t {
  ADV_NO_DATA = 0,        // fewer than MIN_MONITORED_MIN monitored minutes
  ADV_STOPPED,            // pump looked stopped while it should have been running
  ADV_ELEVATED_SHARE,     // elevated for a large share of monitored time
  ADV_REPEATED,           // several separate elevated episodes
  ADV_BRIEF,              // some elevated time, not enough to be a pattern
  ADV_NORMAL              // nothing unusual in the window
};

inline Advice advise(const Summary& s) {
  if (s.monitoredMin < MIN_MONITORED_MIN) return ADV_NO_DATA;
  if (s.stoppedMin >= STOPPED_MIN_LIMIT) return ADV_STOPPED;
  if (s.elevatedShare >= SHARE_LIMIT) return ADV_ELEVATED_SHARE;
  if (s.episodes >= EPISODE_LIMIT && s.elevatedShare >= EPISODE_SHARE_MIN) return ADV_REPEATED;
  if (s.elevatedMin > 0) return ADV_BRIEF;
  return ADV_NORMAL;
}

// True if too many minutes in the window have no data. Show this beside the advice.
inline bool hasDataGaps(const Summary& s) {
  if (s.windowMin == 0) return false;
  return (float)(s.windowMin - s.validMin) / (float)s.windowMin > GAP_LIMIT;
}

}  // namespace vh

#endif
