// test_vib_history.cpp
// Build and run on a laptop:   g++ -std=c++11 -Wall -Wextra -o test_vib test_vib_history.cpp && ./test_vib
//
// This tests the RECORDING and ADVICE code with synthetic data. It does not test the real
// WARNING persistence logic, the sensor, flash storage or any real pump behaviour.
// Synthetic data uses a fixed seed. Reference RMS = 1.0, caution = 1.2, warning = 1.5 (arbitrary units).

#include <stdio.h>
#include <stdint.h>
#include "vib_history.h"

static uint32_t lcgState = 12345u;                 // fixed seed
static float uni() { lcgState = lcgState * 1664525u + 1013904223u; return (lcgState >> 8) / 16777216.0f; }
static float gauss() { float s = 0; for (int i = 0; i < 12; i++) s += uni(); return s - 6.0f; }

static int failures = 0;
#define CHECK(name, cond) do { bool ok_ = (cond); printf("  [%s] %s\n", ok_ ? "PASS" : "FAIL", name); if (!ok_) failures++; } while (0)

static uint8_t flagsFor(float rms) {
  uint8_t f = 0;
  if (rms > 1.2f) f |= vh::F_CAUTION;
  if (rms > 1.5f) f |= vh::F_WARNING;
  return f;
}

// Run 24 h at one sample per second. episodes[] = {startSecond, lengthSeconds, rms}
struct Ep { uint32_t start, len; float rms; };

static void runDay(vh::MinuteRecorder& rec, const Ep* eps, int nEps, float driftTo = 0.0f,
                   uint32_t gapFrom = 0, uint32_t gapTo = 0, uint32_t t0 = 0) {
  for (uint32_t t = 0; t < 86400u; t++) {
    if (gapTo > gapFrom && t >= gapFrom && t < gapTo) continue;   // sensor silent
    float base = 1.0f + driftTo * (float)t / 86400.0f;
    float v = base + 0.05f * gauss();
    for (int i = 0; i < nEps; i++)
      if (t >= eps[i].start && t < eps[i].start + eps[i].len) v = eps[i].rms + 0.05f * gauss();
    rec.update(t0 + t * 1000u, v, flagsFor(v));
  }
  rec.update(t0 + 86400u * 1000u + 1000u, 1.0f, 0);   // close the last minute
}

static const char* adviceName(vh::Advice a) {
  switch (a) {
    case vh::ADV_NO_DATA: return "NO_DATA";
    case vh::ADV_STOPPED: return "STOPPED";
    case vh::ADV_ELEVATED_SHARE: return "ELEVATED_SHARE";
    case vh::ADV_REPEATED: return "REPEATED";
    case vh::ADV_BRIEF: return "BRIEF";
    default: return "NORMAL";
  }
}

static void show(const vh::Summary& s) {
  printf("    window=%u valid=%u monitored=%u elevated=%u warning=%u stopped=%u episodes=%u share=%.3f -> %s\n",
         s.windowMin, s.validMin, s.monitoredMin, s.elevatedMin, s.warningMin, s.stoppedMin, s.episodes,
         s.elevatedShare, adviceName(vh::advise(s)));
}

int main() {
  printf("PLACEHOLDER rules: min monitored %u min, stopped >= %u min, share >= %.2f, episodes >= %u with share >= %.2f, gaps > %.2f\n\n",
         vh::MIN_MONITORED_MIN, vh::STOPPED_MIN_LIMIT, vh::SHARE_LIMIT, vh::EPISODE_LIMIT, vh::EPISODE_SHARE_MIN, vh::GAP_LIMIT);

  // EXPECTED (written before the first run):
  // S1 normal                   -> NORMAL, 0 elevated minutes
  // S2 one 40 s excursion       -> BRIEF, 1 elevated minute, 1 episode
  // S3 six 15 min excursions    -> REPEATED, 6 episodes, 90 elevated minutes
  // S4 one 8 h excursion        -> ELEVATED_SHARE, about 480 elevated minutes
  // S5 slow rise 1.0 -> 1.4     -> ELEVATED_SHARE. CORRECTED after the first run (my arithmetic was wrong):
  //                                a minute is flagged once 20% of samples exceed 1.2, i.e. when the mean reaches
  //                                1.2 - 0.8416*0.05 = 1.158, which is at 9.48 h, so about 871 elevated minutes.
  //                                With episodes merged, expect no more than 3 episodes (first run gave 22 without merging).
  // S7 hover just under the line (mean 1.15 for 6 h) -> no more than 6 episodes (written before the run)
  // S6 pump stops 30 min        -> STOPPED declared 60 to 61 s after the stop, about 29 stopped minutes, advice STOPPED
  // S6b same, but off schedule  -> never STOPPED, advice NORMAL
  // R1 six hour sensor gap      -> 360 invalid minutes, hasDataGaps true
  // R2 150 s stall              -> two empty minutes inserted
  // R3 millis rollover          -> no lost or extra minutes
  // R4 30 h of data             -> count capped at 1440, get(1440) fails

  { printf("S1 normal variation\n");
    vh::MinuteRecorder* r = new vh::MinuteRecorder(); runDay(*r, 0, 0);
    vh::Summary s = r->summarise(1440); show(s);
    CHECK("advice NORMAL", vh::advise(s) == vh::ADV_NORMAL);
    CHECK("0 elevated minutes", s.elevatedMin == 0);
    delete r; }

  { printf("S2 isolated transient (40 s)\n");
    Ep e[] = {{43230, 40, 1.8f}};
    vh::MinuteRecorder* r = new vh::MinuteRecorder(); runDay(*r, e, 1);
    vh::Summary s = r->summarise(1440); show(s);
    CHECK("advice BRIEF", vh::advise(s) == vh::ADV_BRIEF);
    CHECK("1 elevated minute", s.elevatedMin == 1);
    CHECK("1 episode", s.episodes == 1);
    delete r; }

  { printf("S3 recurrent (six 15 min episodes)\n");
    Ep e[] = {{7200,900,1.6f},{21600,900,1.6f},{36000,900,1.6f},{50400,900,1.6f},{64800,900,1.6f},{79200,900,1.6f}};
    vh::MinuteRecorder* r = new vh::MinuteRecorder(); runDay(*r, e, 6);
    vh::Summary s = r->summarise(1440); show(s);
    CHECK("advice REPEATED", vh::advise(s) == vh::ADV_REPEATED);
    CHECK("6 episodes", s.episodes == 6);
    CHECK("90 elevated minutes", s.elevatedMin == 90);
    delete r; }

  { printf("S4 sustained (8 h)\n");
    Ep e[] = {{14400, 28800, 1.6f}};
    vh::MinuteRecorder* r = new vh::MinuteRecorder(); runDay(*r, e, 1);
    vh::Summary s = r->summarise(1440); show(s);
    CHECK("advice ELEVATED_SHARE", vh::advise(s) == vh::ADV_ELEVATED_SHARE);
    CHECK("about 480 elevated minutes (475 to 485)", s.elevatedMin >= 475 && s.elevatedMin <= 485);
    delete r; }

  { printf("S5 slow rise 1.0 to 1.4\n");
    vh::MinuteRecorder* r = new vh::MinuteRecorder(); runDay(*r, 0, 0, 0.4f);
    vh::Summary s = r->summarise(1440); show(s);
    CHECK("advice ELEVATED_SHARE", vh::advise(s) == vh::ADV_ELEVATED_SHARE);
    CHECK("about 871 elevated minutes (850 to 890)", s.elevatedMin >= 850 && s.elevatedMin <= 890);
    CHECK("no more than 3 episodes after merging", s.episodes <= 3);
    delete r; }

  { printf("S7 level hovers just under the caution line for 6 h\n");
    vh::MinuteRecorder* r = new vh::MinuteRecorder();
    for (uint32_t t = 0; t < 86400u; t++) {
      float v = (t >= 21600 && t < 43200) ? 1.15f + 0.05f * gauss() : 1.0f + 0.05f * gauss();
      r->update(t * 1000u, v, flagsFor(v));
    }
    r->update(86400u * 1000u + 1000u, 1.0f, 0);
    vh::Summary s = r->summarise(1440); show(s);
    CHECK("no more than 6 episodes", s.episodes <= 6);
    delete r; }

  { printf("S6 pump stops for 30 min while it should be running\n");
    vh::MinuteRecorder* r = new vh::MinuteRecorder();
    vh::StoppedDetector det(0.05f, 60000UL, 2.0f);
    uint32_t stopStart = 36000, stopEnd = 37800;   // 10:00 to 10:30
    int declaredAt = -1, clearedAt = -1;
    for (uint32_t t = 0; t < 86400u; t++) {
      bool stoppedPhysically = (t >= stopStart && t < stopEnd);
      float dyn = stoppedPhysically ? 0.02f + 0.005f * gauss() : 0.5f + 0.05f * gauss();
      if (dyn < 0.001f) dyn = 0.001f;
      float rms = stoppedPhysically ? 0.27f : 1.0f + 0.05f * gauss();
      bool st = det.update(t * 1000u, dyn, true, true);
      if (st && declaredAt < 0) declaredAt = (int)t;
      if (!st && declaredAt >= 0 && clearedAt < 0) clearedAt = (int)t;
      uint8_t f = flagsFor(rms);
      if (st) f = vh::F_STOPPED;
      r->update(t * 1000u, rms, f);
    }
    r->update(86400u * 1000u + 1000u, 1.0f, 0);
    vh::Summary s = r->summarise(1440); show(s);
    printf("    STOPPED declared at t=%d s (stop began at %u s), cleared at t=%d s\n", declaredAt, stopStart, clearedAt);
    CHECK("declared 60 to 61 s after the stop", declaredAt >= (int)stopStart + 60 && declaredAt <= (int)stopStart + 61);
    CHECK("cleared when the pump restarts", clearedAt == (int)stopEnd);
    CHECK("about 29 stopped minutes (27 to 30)", s.stoppedMin >= 27 && s.stoppedMin <= 30);
    CHECK("advice STOPPED", vh::advise(s) == vh::ADV_STOPPED);
    delete r; }

  { printf("S6b same quiet period but the pump is NOT expected to run (off schedule)\n");
    vh::MinuteRecorder* r = new vh::MinuteRecorder();
    vh::StoppedDetector det(0.05f, 60000UL, 2.0f);
    bool everStopped = false;
    for (uint32_t t = 0; t < 86400u; t++) {
      bool quiet = (t >= 36000 && t < 37800);
      float dyn = quiet ? 0.02f : 0.5f;
      float rms = quiet ? 0.27f : 1.0f + 0.05f * gauss();
      bool st = det.update(t * 1000u, dyn, !quiet, true);  // not expected to run while quiet
      if (st) everStopped = true;
      r->update(t * 1000u, rms, quiet ? (uint8_t)vh::F_OFFSCHED : flagsFor(rms));
    }
    r->update(86400u * 1000u + 1000u, 1.0f, 0);
    vh::Summary s = r->summarise(1440); show(s);
    CHECK("never declared STOPPED", !everStopped);
    CHECK("off-schedule minutes excluded (monitored about 1410)", s.monitoredMin >= 1405 && s.monitoredMin <= 1412);
    CHECK("advice NORMAL", vh::advise(s) == vh::ADV_NORMAL);
    delete r; }

  { printf("Detector edge cases\n");
    vh::StoppedDetector d(0.05f, 60000UL, 2.0f);
    bool st = false;
    for (uint32_t t = 0; t < 30; t++) st = d.update(t * 1000u, 0.02f, true, true);
    CHECK("not stopped after only 30 s quiet", !st);
    d.update(30000u, 0.5f, true, true);
    for (uint32_t t = 31; t < 80; t++) st = d.update(t * 1000u, 0.02f, true, true);
    CHECK("quiet then movement resets the hold timer", !st);
    for (uint32_t t = 80; t < 100; t++) st = d.update(t * 1000u, 0.02f, true, true);
    CHECK("declared once quiet for the full hold", st);
    st = d.update(101000u, 0.08f, true, true);
    CHECK("stays STOPPED between threshold and exit level (hysteresis)", st);
    st = d.update(102000u, 0.2f, true, true);
    CHECK("clears once clearly moving again", !st);
    d.reset();
    for (uint32_t t = 0; t < 100; t++) st = d.update(t * 1000u, 0.02f, false, true);
    CHECK("never stopped when not expected to run", !st);
    d.reset();
    for (uint32_t t = 0; t < 100; t++) st = d.update(t * 1000u, 0.02f, true, false);
    CHECK("never stopped while sample window not full", !st); }

  { printf("R1 six hour sensor gap\n");
    vh::MinuteRecorder* r = new vh::MinuteRecorder(); runDay(*r, 0, 0, 0.0f, 21600, 43200);
    vh::Summary s = r->summarise(1440); show(s);
    CHECK("about 360 invalid minutes (358 to 362)", (s.windowMin - s.validMin) >= 358 && (s.windowMin - s.validMin) <= 362);
    CHECK("hasDataGaps true", vh::hasDataGaps(s));
    delete r; }

  { printf("R2 loop stalls for 150 s\n");
    vh::MinuteRecorder r;
    for (uint32_t t = 0; t < 60; t++) r.update(t * 1000u, 1.0f, 0);        // fills minute 0
    r.update(60000u, 1.0f, 0);                                              // closes minute 0
    int before = r.count();
    r.update(60000u + 150000u, 1.0f, 0);                                    // 150 s later
    int after = r.count();
    printf("    completed minutes before=%d after=%d\n", before, after);
    vh::MinuteRecord m;
    r.get(0, m); bool newestEmpty = !(m.flags & vh::F_VALID);
    CHECK("two empty minutes inserted", after - before == 2 && newestEmpty); }

  { printf("R3 millis rollover\n");
    vh::MinuteRecorder r;
    uint32_t t0 = 0xFFFFFFFFu - 120000u;      // wraps after 2 minutes
    for (uint32_t t = 0; t < 600; t++) r.update(t0 + t * 1000u, 1.0f, 0);
    r.update(t0 + 600u * 1000u + 1000u, 1.0f, 0);
    printf("    completed minutes = %d (expected 10)\n", r.count());
    vh::Summary s = r.summarise(10);
    CHECK("10 minutes recorded across the wrap, all valid", r.count() == 10 && s.validMin == 10); }

  { printf("R4 ring buffer wraps\n");
    vh::MinuteRecorder* r = new vh::MinuteRecorder();
    for (uint32_t t = 0; t < 30u * 3600u; t++) r->update(t * 1000u, 1.0f, 0);
    r->update(30u * 3600u * 1000u + 1000u, 1.0f, 0);
    vh::MinuteRecord m;
    CHECK("count capped at 1440", r->count() == 1440);
    CHECK("get(1439) works and get(1440) fails", r->get(1439, m) && !r->get(1440, m));
    delete r; }

  { printf("windowStd\n");
    float rest[5] = {10.06f, 10.08f, 10.05f, 10.07f, 10.06f};
    float moving[5] = {9.5f, 10.9f, 9.8f, 11.2f, 10.0f};
    float a = vh::windowStd(rest, 5), b = vh::windowStd(moving, 5);
    printf("    rest std=%.3f moving std=%.3f\n", a, b);
    CHECK("a constant offset does not change the spread", vh::windowStd(rest, 5) < 0.03f);
    CHECK("moving signal has a much larger spread", b > 10.0f * a); }

  printf("\n%s (%d failure%s)\n", failures ? "SOME CHECKS FAILED" : "ALL CHECKS PASSED", failures, failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
