// The extender panel's reading of ExtenderStatus (connect/EXTENDER.md K4/K5),
// computed as one pure step so every display rule is deterministic and
// testable, and so the panel widget only draws what this decided.
//
// Left to right the panel shows: one hollow ring per ACTIVE extender in its
// own color (active = carrying at least one live connection right now, which
// is ExtenderInfo::InUse > 0), the count "N of M" where M is every usable
// directory entry (active state, not on hold), and a gossip status dot --
// green connected, yellow connecting, red disconnected -- with the state's
// label and the count of records and revocations applied in the trailing 60 s.
//
// Header-only and free of GTK and the SDK so the unit tests need no vendored
// headers (tests/ExtenderStatusPresentationTest.cpp).
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace urnw::extender {

// Mirrors of the SDK's gossip-state vocabulary (URNET_EXTENDER_GOSSIP_STATE_*),
// kept here so this header stays SDK-free.
inline constexpr const char* kGossipStateConnected = "connected";
inline constexpr const char* kGossipStateConnecting = "connecting";
inline constexpr const char* kGossipStateDisconnected = "disconnected";

enum class GossipState { Connected, Connecting, Disconnected };

// The three status-dot colors, named by role rather than by pixel so this
// header carries no palette.
enum class StatusDot { Green, Yellow, Red };

// A state this build does not recognize -- and an ABSENT state, which is what
// an older SDK sends -- reads as DISCONNECTED. Green would claim a mesh that
// may not exist, and "nothing in progress" is the honest default for a word
// this app cannot interpret.
inline GossipState GossipStateFor(const std::string& raw) {
  if (raw == kGossipStateConnected) return GossipState::Connected;
  if (raw == kGossipStateConnecting) return GossipState::Connecting;
  return GossipState::Disconnected;
}

inline StatusDot DotFor(GossipState state) {
  switch (state) {
    case GossipState::Connected: return StatusDot::Green;
    case GossipState::Connecting: return StatusDot::Yellow;
    case GossipState::Disconnected: return StatusDot::Red;
  }
  return StatusDot::Red;
}

// The localization-store key id for the state's label, and the English source
// that key carries. The store has no bare `connecting` key -- the gossip
// panel's yellow state is `gossip_connecting` -- so the three do not share a
// prefix; that is the store's shape, not a choice made here.
inline const char* StateLabelKey(GossipState state) {
  switch (state) {
    case GossipState::Connected: return "connected";
    case GossipState::Connecting: return "gossip_connecting";
    case GossipState::Disconnected: return "disconnected";
  }
  return "disconnected";
}

inline const char* StateLabelEnglish(GossipState state) {
  switch (state) {
    case GossipState::Connected: return "Connected";
    case GossipState::Connecting: return "Connecting";
    case GossipState::Disconnected: return "Disconnected";
  }
  return "Disconnected";
}

// One directory entry, reduced to the fields the panel reads.
struct Entry {
  std::string ip;
  std::string colorHex;
  int64_t inUse = 0;
};

// A malformed or runaway status must not build ten thousand widgets. The
// overflow is counted, not drawn: the "N of M" figure beside the rings already
// states the real number.
//
// NOT ExtenderRingGeometry.hpp's kMaxRings, which is the three-ring cap around
// a single provider dot. These rings are one per live extender across the whole
// device, and the two headers share a namespace.
inline constexpr int kMaxPanelRings = 32;

struct Panel {
  // false when there is no status at all (no session, no daemon): the panel
  // hides rather than claiming zero extenders, which is a different reading.
  bool known = false;
  // one ring per active extender, in the SDK's order, in its own color
  std::vector<std::string> ringColorHexes;
  // active extenders the ring cap dropped; 0 normally
  int hiddenRings = 0;
  int64_t active = 0;   // ExtenderStatus::ActiveCount (K5's redefined active)
  int64_t reserve = 0;  // ExtenderStatus::ReserveCount (usable, not on hold)
  GossipState gossip = GossipState::Disconnected;
  int64_t eventsPerMinute = 0;

  StatusDot dot() const { return DotFor(gossip); }
  const char* stateLabelKey() const { return StateLabelKey(gossip); }
  const char* stateLabelEnglish() const { return StateLabelEnglish(gossip); }

  // By value, so the panel can drop a push that changes nothing rather than
  // rebuilding its ring strip once a second forever.
  friend bool operator==(const Panel& a, const Panel& b) {
    return a.known == b.known && a.ringColorHexes == b.ringColorHexes &&
           a.hiddenRings == b.hiddenRings && a.active == b.active && a.reserve == b.reserve &&
           a.gossip == b.gossip && a.eventsPerMinute == b.eventsPerMinute;
  }
  friend bool operator!=(const Panel& a, const Panel& b) { return !(a == b); }
};

// The panel's whole reading, from the fields of one ExtenderStatus.
//
// The rings come from the ENTRIES (only the SDK knows which address is in use
// and what color it wears) while the two figures come from the status COUNTS,
// which is what K5 defines them to be. The two can legitimately disagree for
// one sample -- a status whose extender list was truncated, or a count updated
// a tick apart -- and when they do the figures win, because they are the
// authority the design names.
//
// Every count is clamped at zero: a negative figure is a bug somewhere else
// and "-1 of 3" is not a reading a user can act on.
inline Panel PanelFor(bool haveStatus, const std::vector<Entry>& extenders, int64_t activeCount,
                      int64_t reserveCount, const std::string& gossipState,
                      int64_t eventCountLastMinute) {
  Panel out;
  if (!haveStatus) return out;
  out.known = true;
  out.active = std::max<int64_t>(activeCount, 0);
  out.reserve = std::max<int64_t>(reserveCount, 0);
  out.gossip = GossipStateFor(gossipState);
  out.eventsPerMinute = std::max<int64_t>(eventCountLastMinute, 0);
  for (const auto& entry : extenders) {
    if (entry.inUse <= 0) continue;  // "active" is carrying traffic right now
    if (static_cast<int>(out.ringColorHexes.size()) >= kMaxPanelRings) {
      ++out.hiddenRings;
      continue;
    }
    out.ringColorHexes.push_back(entry.colorHex);
  }
  return out;
}

}  // namespace urnw::extender
