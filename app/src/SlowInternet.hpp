// The slow-internet indicator (apple/DELAY.md §1, §4; connect/DELAY.md §10):
// the device's transport delay as SdkHost publishes it, what the Connect
// page's pill, the tray and the desktop notification make of it, the number
// formats, and the notification's dwell, so it does not flap.
//
// The SDK reports a quantized quality per direction, 0 (normal) to 1 (worst),
// and the indicator follows the worse direction. It shows only while that is
// above 0 AND the device remote is connected: while the daemon is out of
// reach the last level is stale, and a stale level is never shown. The pill
// follows the level as it comes, and says "Internet back to normal" for 3 s
// when it returns to 0. The tray and the notification appear only once the
// level has been above 0 for 30 s of the last 60 s, then stay for 60 s at
// least, and say "Internet back to normal" for 3 s before they go.
//
// Pure and dependency-free (tests/SlowInternetTest.cpp). The GUI translates
// the Copy pairs with T_ where it shows them. Not thread safe: SdkHost holds
// its Feed under a lock of its own, everything else is the GTK main loop's.
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <algorithm>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <string>

#include "TunnelGeometry.hpp"

namespace urnw::slow_internet {

// The SDK's TransportDelay, copied out of the SDK's type.
struct Delay {
  double upQuality = 0;    // 0..1 in quality steps
  double downQuality = 0;
  int64_t upBitsPerSecond = 0;  // the worst network-limited rate; 0 for none
  int64_t downBitsPerSecond = 0;
  int64_t upQueueDelayMillis = 0;  // the worst standing queue; 0 for none
  int64_t downQueueDelayMillis = 0;
  // the worst reading came from a DNS-carried fallback transport: the copy
  // says URnetwork is using a slower fallback connection
  bool fallback = false;

  bool operator==(const Delay& o) const {
    return upQuality == o.upQuality && downQuality == o.downQuality &&
           upBitsPerSecond == o.upBitsPerSecond && downBitsPerSecond == o.downBitsPerSecond &&
           upQueueDelayMillis == o.upQueueDelayMillis &&
           downQueueDelayMillis == o.downQueueDelayMillis && fallback == o.fallback;
  }
  bool operator!=(const Delay& o) const { return !(*this == o); }
};

// One reading: the delay, and whether it may be shown at all.
struct Reading {
  // the device remote is connected (SdkHost), and a session is up on it
  // (MainWindow narrows it): false hides every surface
  bool available = false;
  Delay delay;
  // SdkHost's publish order; a reading older than one applied is dropped
  uint64_t serial = 0;

  // The same reading, whatever its serial.
  bool SameAs(const Reading& o) const { return available == o.available && delay == o.delay; }
};

inline double Quality(const Delay& delay) { return std::max(delay.upQuality, delay.downQuality); }
// The tunnel's squares, 0 when nothing shows.
inline int Depth(const Reading& reading) {
  return reading.available ? tunnel::DepthForQuality(Quality(reading.delay)) : 0;
}
// 1 to 4 while slow (the tray's art), 0 when nothing shows.
inline int Level(const Reading& reading) {
  return reading.available ? tunnel::LevelForQuality(Quality(reading.delay)) : 0;
}

// ---- number formats ------------------------------------------------------------
// The units are not localized. A value of 0 (none measured) is "", which
// hides its line.

// "640 ms" under a second, "1.2 s" under ten, "12 s" from there.
inline std::string FormatDelayMillis(int64_t millis) {
  if (millis <= 0) return {};
  if (millis < 1000) return std::to_string(millis) + " ms";
  const int64_t tenths = (millis + 50) / 100;
  if (tenths < 100) return std::to_string(tenths / 10) + "." + std::to_string(tenths % 10) + " s";
  return std::to_string((millis + 500) / 1000) + " s";
}

// "48 kb/s" (whole kilobits, 1 at least) under a megabit, "1.4 Mb/s" from there.
inline std::string FormatBitsPerSecond(int64_t bitsPerSecond) {
  if (bitsPerSecond <= 0) return {};
  const int64_t kilobits = std::max<int64_t>(1, (bitsPerSecond + 500) / 1000);
  if (kilobits < 1000) return std::to_string(kilobits) + " kb/s";
  const int64_t tenths = (bitsPerSecond + 50'000) / 100'000;
  return std::to_string(tenths / 10) + "." + std::to_string(tenths % 10) + " Mb/s";
}

// One direction's value: the delay and the limited rate, whichever were
// measured ("640 ms · 48 kb/s"), or "" for a direction with neither.
inline std::string DirectionValue(int64_t delayMillis, int64_t bitsPerSecond) {
  const std::string delay = FormatDelayMillis(delayMillis);
  const std::string rate = FormatBitsPerSecond(bitsPerSecond);
  if (delay.empty()) return rate;
  if (rate.empty()) return delay;
  return delay + " · " + rate;
}

// ---- copy ------------------------------------------------------------------------
// The store's key and English for each line (tagged linux in the store).
struct Copy {
  const char* key;
  const char* english;
};

// The tray's line beside the connection state.
inline Copy TrayLineCopy(bool fallback) {
  if (fallback) return Copy{"slow_internet_fallback", "Fallback connection"};
  return Copy{"slow_internet", "Slow internet"};
}

// The pill's label.
inline Copy PillLabelCopy(bool fallback) {
  if (fallback) return Copy{"slow_internet_fallback_short", "Fallback"};
  return Copy{"slow_internet_poor_signal", "Poor signal"};
}

// The notification's title.
inline Copy TitleCopy(bool fallback) {
  if (fallback) return Copy{"slow_internet_fallback_title", "Using a slower fallback connection"};
  return Copy{"slow_internet", "Slow internet"};
}

// The notification's first line.
inline Copy StillConnectedCopy() {
  return Copy{"slow_internet_still_connected", "URnetwork is still connected"};
}

inline Copy UploadDelayCopy() { return Copy{"slow_internet_upload_delay", "Upload delay"}; }
inline Copy DownloadDelayCopy() { return Copy{"slow_internet_download_delay", "Download delay"}; }

// The last 3 s of an episode: the pill's words, the notification's title and
// the tray's line.
inline Copy RecoveredCopy() { return Copy{"slow_internet_recovered", "Internet back to normal"}; }
inline Copy RecoveredDetailCopy() {
  return Copy{"slow_internet_recovered_detail", "Delays cleared"};
}

// Settings' switch.
inline Copy NotifyToggleCopy() {
  return Copy{"slow_internet_notify_toggle", "Notify me when my internet is slow"};
}

// The app_prefs.json key behind Settings' switch, true when absent.
inline constexpr const char* kNotifyPrefKey = "slow_internet_notify";

// ---- the Connect page's pill -------------------------------------------------------
// What the pill says (apple/DELAY.md §5.2, as apple's SlowInternetPillContent
// says it), as a value so its rules are tested: "Poor signal", or "Fallback"
// on the fallback connection; then each direction's standing delay, or its
// limited rate when it has no delay, and nothing when it has neither. The
// larger delay is the worse direction, drawn amber. Recovered, the pill says
// "Internet back to normal" instead.

enum class Direction { None, Up, Down };

// The direction with the larger delay, the worse one; equal delays mark
// neither.
inline Direction WorseDirection(int64_t upDelayMillis, int64_t downDelayMillis) {
  if (downDelayMillis < upDelayMillis) return Direction::Up;
  if (upDelayMillis < downDelayMillis) return Direction::Down;
  return Direction::None;
}

struct PillContent {
  // the label: "Fallback" rather than "Poor signal"
  bool fallback = false;
  // "640 ms", or "48 kb/s" with no delay; "" hides the direction
  std::string up;
  std::string down;
  // the larger delay's direction; None when the delays are equal
  Direction worse = Direction::None;
  // the few seconds after a return to normal, before the pill leaves
  bool recovered = false;

  bool operator==(const PillContent& o) const {
    return fallback == o.fallback && up == o.up && down == o.down && worse == o.worse &&
           recovered == o.recovered;
  }
  bool operator!=(const PillContent& o) const { return !(*this == o); }

  // What the pill says for a slow reading.
  static PillContent Slow(const Delay& delay) {
    // the standing delay, else the limited rate, else nothing
    const auto number = [](int64_t delayMillis, int64_t bitsPerSecond) {
      const std::string text = FormatDelayMillis(delayMillis);
      return text.empty() ? FormatBitsPerSecond(bitsPerSecond) : text;
    };
    PillContent content;
    content.fallback = delay.fallback;
    content.up = number(delay.upQueueDelayMillis, delay.upBitsPerSecond);
    content.down = number(delay.downQueueDelayMillis, delay.downBitsPerSecond);
    content.worse = WorseDirection(delay.upQueueDelayMillis, delay.downQueueDelayMillis);
    return content;
  }

  // With room for one direction: the worse one, or the download when
  // neither is worse (the upload when only it shows).
  PillContent WorseOnly() const {
    PillContent content = *this;
    Direction keep = worse;
    if (keep == Direction::None) keep = down.empty() ? Direction::Up : Direction::Down;
    if (keep == Direction::Up) {
      content.down.clear();
    } else {
      content.up.clear();
    }
    return content;
  }

  // With no room for numbers: the tunnel and the label.
  PillContent LabelOnly() const {
    PillContent content = *this;
    content.up.clear();
    content.down.clear();
    return content;
  }

  // What the pill shows once `reading` arrives, after `shown` (none when the
  // pill is not up, or is leaving):
  //   - while the level is above 0, the slow content, which also takes the
  //     recovered state's place;
  //   - on a return to normal from the slow content, the recovered state,
  //     which then holds;
  //   - none, the pill leaving, when the reading goes away (unavailable: the
  //     remote or the session went). That is no recovery.
  static std::optional<PillContent> Next(const std::optional<PillContent>& shown,
                                         const Reading& reading) {
    if (0 < Level(reading)) return Slow(reading.delay);
    if (!reading.available || !shown) return std::nullopt;
    if (shown->recovered) return shown;
    PillContent recovered = shown->LabelOnly();
    recovered.worse = Direction::None;
    recovered.recovered = true;
    return recovered;
  }
};

// The pill's one accessible name: its label, or "Internet back to normal"
// while recovered, then each direction it shows ("Poor signal, Upload delay
// 800 ms, Download delay 1.9 s"). `translate` is T_ in the app.
inline std::string PillAccessibleName(const PillContent& content,
                                      const std::function<std::string(const Copy&)>& translate) {
  std::string name =
      translate(content.recovered ? RecoveredCopy() : PillLabelCopy(content.fallback));
  if (!content.up.empty()) name += ", " + translate(UploadDelayCopy()) + " " + content.up;
  if (!content.down.empty()) name += ", " + translate(DownloadDelayCopy()) + " " + content.down;
  return name;
}

// ---- SdkHost's feed ----------------------------------------------------------------
// What the bound device last said and whether its remote is connected, by
// device generation, and the reading that follows from them. A push or an
// edge from a device that has since been torn down is dropped. While the
// remote is disconnected nothing shows, and its last value is forgotten: the
// sync that reconnects it replays the current level (DeviceLocalRpc
// SyncReverse), so nothing stale survives a reconnect either.
//
// Registration replays nothing, so SdkHost reads both values once after
// registering. Such a read applies only when no push of its kind landed since
// the revision taken before it: a push is always newer than the read.
class Feed {
 public:
  // A device was bound: nothing is known about it yet. Its generation.
  uint64_t Bind() {
    Reset();
    return generation_;
  }
  // The device is gone, or its daemon's tunnel is: nothing shows until the
  // next Bind.
  void Clear() { Reset(); }

  // The device's listener. True when the reading changed.
  bool NoteDelay(uint64_t generation, const Delay& delay) {
    if (generation != generation_) return false;
    ++delayRevision_;
    if (delay == delay_) return false;
    delay_ = delay;
    return remoteConnected_;
  }
  // The remote's connection edge. True when the reading changed.
  bool NoteRemote(uint64_t generation, bool connected) {
    if (generation != generation_) return false;
    ++remoteRevision_;
    if (connected == remoteConnected_) return false;
    remoteConnected_ = connected;
    if (!connected) delay_ = Delay{};
    return true;
  }

  uint64_t Generation() const { return generation_; }
  uint64_t DelayRevision() const { return delayRevision_; }
  uint64_t RemoteRevision() const { return remoteRevision_; }
  // The reads after registering, taken after `revision` was.
  bool SeedDelay(uint64_t generation, uint64_t revision, const Delay& delay) {
    if (revision != delayRevision_) return false;
    return NoteDelay(generation, delay);
  }
  bool SeedRemote(uint64_t generation, uint64_t revision, bool connected) {
    if (revision != remoteRevision_) return false;
    return NoteRemote(generation, connected);
  }

  Reading Current() const {
    Reading reading;
    reading.available = remoteConnected_;
    if (remoteConnected_) reading.delay = delay_;
    return reading;
  }

 private:
  void Reset() {
    ++generation_;
    remoteConnected_ = false;
    delay_ = Delay{};
  }

  uint64_t generation_ = 0;
  uint64_t delayRevision_ = 0;
  uint64_t remoteRevision_ = 0;
  bool remoteConnected_ = false;
  Delay delay_;
};

// ---- the notification ----------------------------------------------------------------
// The dwell (the contract's numbers, the same on every platform's
// notification surface). Times are on one monotonic clock, in milliseconds.
inline constexpr int64_t kDwellWindowMillis = 60 * 1000;
inline constexpr int64_t kDwellSlowMillis = 30 * 1000;  // slow for this much of the window
inline constexpr int64_t kMinShownMillis = 60 * 1000;   // once shown, at least this long
inline constexpr int64_t kRecoveredMillis = 3 * 1000;   // "back to normal", then withdrawn
// The SDK tells a level change only, while the numbers move within a level:
// while slow they are read again this often, as the other apps do, and
// while the window is up, where the pill shows them, as often as the Apple
// pill's are.
inline constexpr unsigned kRefreshSeconds = 30;
inline constexpr unsigned kPillRefreshSeconds = 10;

// Whether the level was above 0, over the last kDwellWindowMillis.
class SlowHistory {
 public:
  // The level as of `nowMillis`. Times never go backwards.
  void Note(bool slow, int64_t nowMillis) {
    if (changes_.empty() || changes_.back().slow != slow) changes_.push_back(Change{nowMillis, slow});
    // a change is needed while the one after it is still inside the window
    while (changes_.size() >= 2 && changes_[1].atMillis <= nowMillis - kDwellWindowMillis) {
      changes_.pop_front();
    }
  }
  void Clear() { changes_.clear(); }

  // How long it was slow in the window ending at `nowMillis`.
  int64_t SlowMillis(int64_t nowMillis) const {
    const int64_t start = nowMillis - kDwellWindowMillis;
    int64_t total = 0;
    for (size_t i = 0; i < changes_.size(); ++i) {
      if (!changes_[i].slow) continue;
      const int64_t from = std::max(changes_[i].atMillis, start);
      const int64_t to = i + 1 < changes_.size() ? changes_[i + 1].atMillis : nowMillis;
      if (from < to) total += to - from;
    }
    return total;
  }

  // When SlowMillis first reaches `required` if it stays slow from
  // `nowMillis` on: `nowMillis` itself or later, or -1 when it is not slow.
  // The window gains what passes now and loses what slides out at its start,
  // so it grows only while what slides out was not slow.
  int64_t ReachesAtMillis(int64_t required, int64_t nowMillis) const {
    if (changes_.empty() || !changes_.back().slow) return -1;
    int64_t need = required - SlowMillis(nowMillis);
    if (need <= 0) return nowMillis;
    int64_t start = nowMillis - kDwellWindowMillis;
    // the history from the window's start: before the first change it was not slow
    for (size_t i = 0; i <= changes_.size(); ++i) {
      const int64_t from = std::max(start, i == 0 ? start : changes_[i - 1].atMillis);
      const int64_t to = i < changes_.size() ? changes_[i].atMillis : nowMillis;
      const bool slow = i > 0 && changes_[i - 1].slow;
      if (from >= to) continue;
      if (!slow) {
        if (to - from >= need) return from + need + kDwellWindowMillis;
        need -= to - from;
      }
      start = to;
    }
    // by now + window the whole window is slow
    return nowMillis + kDwellWindowMillis;
  }

 private:
  struct Change {
    int64_t atMillis;
    bool slow;
  };
  std::deque<Change> changes_;
};

// What the notification says: the slow state with its numbers, or the
// recovered one.
struct Notice {
  bool recovered = false;
  bool fallback = false;
  int64_t upDelayMillis = 0;
  int64_t downDelayMillis = 0;
  int64_t upBitsPerSecond = 0;
  int64_t downBitsPerSecond = 0;

  bool operator==(const Notice& o) const {
    return recovered == o.recovered && fallback == o.fallback &&
           upDelayMillis == o.upDelayMillis && downDelayMillis == o.downDelayMillis &&
           upBitsPerSecond == o.upBitsPerSecond && downBitsPerSecond == o.downBitsPerSecond;
  }
  bool operator!=(const Notice& o) const { return !(*this == o); }
};

inline Notice SlowNotice(const Delay& delay) {
  Notice notice;
  notice.fallback = delay.fallback;
  notice.upDelayMillis = delay.upQueueDelayMillis;
  notice.downDelayMillis = delay.downQueueDelayMillis;
  notice.upBitsPerSecond = delay.upBitsPerSecond;
  notice.downBitsPerSecond = delay.downBitsPerSecond;
  return notice;
}

inline Notice RecoveredNotice() {
  Notice notice;
  notice.recovered = true;
  return notice;
}

struct NoticeText {
  std::string title;
  std::string body;
};

// The notification's title and body. `translate` is T_ in the app.
inline NoticeText ComposeNotice(const Notice& notice,
                                const std::function<std::string(const Copy&)>& translate) {
  NoticeText text;
  if (notice.recovered) {
    text.title = translate(RecoveredCopy());
    text.body = translate(RecoveredDetailCopy());
    return text;
  }
  text.title = translate(TitleCopy(notice.fallback));
  text.body = translate(StillConnectedCopy());
  const std::string up = DirectionValue(notice.upDelayMillis, notice.upBitsPerSecond);
  if (!up.empty()) text.body += "\n" + translate(UploadDelayCopy()) + ": " + up;
  const std::string down = DirectionValue(notice.downDelayMillis, notice.downBitsPerSecond);
  if (!down.empty()) text.body += "\n" + translate(DownloadDelayCopy()) + ": " + down;
  return text;
}

// Posts, replaces and withdraws the notification through a sink with
// Post(const Notice&) and Withdraw(), modeled on balance_notice::Tracker: one
// notification per episode, its content replaced when it changes. An episode
// starts with the post and ends with the withdrawal, which forgets the
// history: the next one has to be slow for its own 30 s. Unavailable (the
// remote or the session went) or switched off withdraws at once, with no
// recovered state: nothing says the connection recovered.
//
// The dwell moves with time as well as with readings: once Observe returns,
// NextDeadlineMillis says when to observe again with the same reading.
class NoticeTracker {
 public:
  enum class Phase { Hidden, Slow, Recovered };

  template <class Sink>
  void Observe(const Reading& reading, bool enabled, int64_t nowMillis, Sink& sink) {
    nowMillis_ = nowMillis;
    if (!reading.available || !enabled) {
      if (phase_ != Phase::Hidden) Withdraw(sink);
      history_.Clear();
      slow_ = false;
      return;
    }
    slow_ = Level(reading) > 0;
    history_.Note(slow_, nowMillis);
    switch (phase_) {
      case Phase::Hidden:
        if (slow_ && kDwellSlowMillis <= history_.SlowMillis(nowMillis)) {
          phase_ = Phase::Slow;
          shownAtMillis_ = nowMillis;
          Post(SlowNotice(reading.delay), sink);
        }
        break;
      case Phase::Slow:
        if (slow_) {
          Post(SlowNotice(reading.delay), sink);
        } else if (kMinShownMillis <= nowMillis - shownAtMillis_) {
          // it stayed its minute: back to normal, for a moment
          phase_ = Phase::Recovered;
          recoveredAtMillis_ = nowMillis;
          Post(RecoveredNotice(), sink);
        }
        // else it holds what it said until its minute is up
        break;
      case Phase::Recovered:
        if (slow_) {
          // slow again before it went: the same episode
          phase_ = Phase::Slow;
          Post(SlowNotice(reading.delay), sink);
        } else if (kRecoveredMillis <= nowMillis - recoveredAtMillis_) {
          Withdraw(sink);
          history_.Clear();
        }
        break;
    }
  }

  // When to observe again with no new reading, or -1 for no need.
  int64_t NextDeadlineMillis() const {
    switch (phase_) {
      case Phase::Hidden:
        return slow_ ? history_.ReachesAtMillis(kDwellSlowMillis, nowMillis_) : -1;
      case Phase::Slow:
        return slow_ ? -1 : shownAtMillis_ + kMinShownMillis;
      case Phase::Recovered:
        return slow_ ? -1 : recoveredAtMillis_ + kRecoveredMillis;
    }
    return -1;
  }

  Phase phase() const { return phase_; }
  bool Shown() const { return phase_ != Phase::Hidden; }

 private:
  // replaces the notification only when what it says changed
  template <class Sink>
  void Post(const Notice& notice, Sink& sink) {
    if (posted_ && notice == notice_) return;
    posted_ = true;
    notice_ = notice;
    sink.Post(notice);
  }
  // ends the episode
  template <class Sink>
  void Withdraw(Sink& sink) {
    phase_ = Phase::Hidden;
    posted_ = false;
    sink.Withdraw();
  }

  Phase phase_ = Phase::Hidden;
  SlowHistory history_;
  bool slow_ = false;
  int64_t nowMillis_ = 0;
  int64_t shownAtMillis_ = 0;
  int64_t recoveredAtMillis_ = 0;
  bool posted_ = false;
  Notice notice_;
};

// The tray's slow state. The tray stays in view outside the app, like the
// notification, so it follows the same dwell whatever the notification
// setting (apple/DELAY.md §3), as the Windows tray does: the tunnel only once
// the connection has been slow for 30 s of the last minute, at the level as
// it comes and held at its last one through the minimum stay, then "Internet
// back to normal" on the tray's own icon for a moment.
class TraySlowTracker {
 public:
  struct View {
    // 1 to 4, or 0 for the tray's own icon
    int level = 0;
    // the tooltip's slow line, when there is one
    std::optional<Copy> line;
  };

  void Observe(const Reading& reading, int64_t nowMillis) {
    NoSink sink;
    tracker_.Observe(reading, /*enabled=*/true, nowMillis, sink);
    switch (tracker_.phase()) {
      case NoticeTracker::Phase::Hidden:
        view_ = View{};
        break;
      case NoticeTracker::Phase::Slow:
        // back to 0 inside the minimum stay: it holds what it showed
        if (0 < Level(reading)) {
          view_.level = Level(reading);
          view_.line = TrayLineCopy(reading.delay.fallback);
        }
        break;
      case NoticeTracker::Phase::Recovered:
        view_.level = 0;
        view_.line = RecoveredCopy();
        break;
    }
  }

  // When to observe again with no new reading, or -1 for no need.
  int64_t NextDeadlineMillis() const { return tracker_.NextDeadlineMillis(); }
  const View& view() const { return view_; }

 private:
  struct NoSink {
    void Post(const Notice&) {}
    void Withdraw() {}
  };
  NoticeTracker tracker_;
  View view_;
};

}  // namespace urnw::slow_internet
