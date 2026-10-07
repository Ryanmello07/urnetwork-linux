// What the connect page's activity list shows of the routing-decision feed:
// the verdict filter (All, Blocked, Tunnelled, Bypassed), the host/address
// search, the group-by-host fold, the header count that reads "N hosts of M"
// while any of them holds rows back, and the verdict ratio bar under the
// header. All of it is view-side over the feeds the page already caches, so
// the filter and a push can never disagree about the feed.
//
// The three verdicts are the ones the row dots print: blocked, tunnelled
// (allowed through the tunnel) and bypassed (allowed around it, a local
// route). Hosts and addresses are ASCII on the wire (an IDN travels as
// punycode), so folding ASCII case is the whole case-insensitive match.
//
// No GTK, no SDK: ConnectPage feeds it the decision's fields and renders what
// comes back. Unit-tested in tests/ConnectionFilterTest.cpp.
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace urnw::connection_filter {

// The verdict segments: everything, or one of the three verdicts.
enum class Verdict { All, Blocked, Tunnelled, Bypassed };

// The fields of one routing decision the filter reads. The lists are the
// decision's own (null when the feed sent none), never copies: the filter
// runs over the whole feed on every push.
struct Decision {
  const std::vector<std::string>* hosts = nullptr;
  const std::vector<std::string>* ips = nullptr;
  const std::vector<std::string>* matchedHosts = nullptr;
  const std::vector<std::string>* matchedIps = nullptr;
  bool block = false;
  bool local = false;
};

// A decision with this block and local reading shows under the segment.
inline bool VerdictPasses(Verdict verdict, bool block, bool local) {
  switch (verdict) {
    case Verdict::Blocked:
      return block;
    case Verdict::Tunnelled:
      return !block && !local;
    case Verdict::Bypassed:
      return !block && local;
    case Verdict::All:
      break;
  }
  return true;
}

// The text with ASCII letters lowercased and every other byte kept.
inline std::string AsciiLower(std::string text) {
  for (char& c : text) {
    if ('A' <= c && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  }
  return text;
}

// The search field's text as the filter matches it: trimmed and lowercased.
inline std::string NormalizeQuery(const std::string& text) {
  const char* space = " \t\r\n\f\v";
  const size_t first = text.find_first_not_of(space);
  if (first == std::string::npos) return std::string();
  const size_t last = text.find_last_not_of(space);
  return AsciiLower(text.substr(first, last - first + 1));
}

// A substring of any host or address the row or the inspector can print. The
// query is NormalizeQuery's; an empty one passes everything.
inline bool QueryPasses(const std::string& query, const Decision& decision) {
  if (query.empty()) return true;
  for (const auto* values :
       {decision.matchedHosts, decision.hosts, decision.matchedIps, decision.ips}) {
    if (!values) continue;
    for (const auto& value : *values) {
      if (AsciiLower(value).find(query) != std::string::npos) return true;
    }
  }
  return false;
}

// The decision shows under both the segment and the search.
inline bool Passes(Verdict verdict, const std::string& query, const Decision& decision) {
  return VerdictPasses(verdict, decision.block, decision.local) && QueryPasses(query, decision);
}

// A filter is holding rows back.
inline bool FilterActive(Verdict verdict, const std::string& query) {
  return verdict != Verdict::All || !query.empty();
}

// The header offers Clear while the list is anything but the whole feed,
// flat: a filter, the search or the fold.
inline bool ClearOffered(Verdict verdict, const std::string& query, bool grouped) {
  return FilterActive(verdict, query) || grouped;
}

// The list, rather than the "no session" sentence, shows while connected and
// a row shows, or while a filter hides every row of a feed that has some: an
// empty list under the filter says what happened, and the sentence would
// contradict it.
inline bool ShowList(bool connected, bool anyRow, bool filterActive, int64_t total) {
  return connected && (anyRow || (filterActive && 0 < total));
}

// The header's count: `shown` hosts, and "of `of`" while ofTotal holds.
struct Count {
  int64_t shown = 0;
  bool ofTotal = false;
  int64_t of = 0;
};

// `passed` decisions passed the filter out of `total` in the feed. Folded,
// the count is the groups of the decisions that passed: "8 hosts of 30".
inline Count CountFor(Verdict verdict, const std::string& query, bool grouped, int64_t groups,
                      int64_t passed, int64_t total) {
  Count count;
  if (grouped) {
    count.shown = groups;
    count.ofTotal = true;
    count.of = passed;
    return count;
  }
  count.ofTotal = FilterActive(verdict, query);
  count.shown = count.ofTotal ? passed : total;
  count.of = total;
  return count;
}

// ---- group by host ---------------------------------------------------------

// One decision that passed the filter, as the fold reads it.
struct FoldMember {
  std::string host;  // the display host: the row title's precedence
  bool block = false;
  bool local = false;
  int64_t timeMs = 0;
  int64_t byteCount = 0;
  int64_t packetCount = 0;
};

// One display host's decisions, summed. Its verdict is the precedence the
// row dots print: blocked if any decision blocked, else bypassed if any went
// around the tunnel, else tunnelled.
struct Group {
  std::string host;
  int64_t connections = 0;
  int64_t byteCount = 0;
  int64_t packetCount = 0;
  int64_t latestMs = 0;
  bool anyBlocked = false;
  bool anyLocal = false;
  size_t newestMember = 0;  // the first of the host's members (newest first)

  bool blocked() const { return anyBlocked; }
  bool bypassed() const { return !anyBlocked && anyLocal; }
};

// The members (newest first) folded by host, latest decision first; a tie
// keeps the feed's order.
inline std::vector<Group> FoldGroups(const std::vector<FoldMember>& members) {
  std::vector<Group> groups;
  std::unordered_map<std::string, size_t> at;
  for (size_t i = 0; i < members.size(); ++i) {
    const FoldMember& member = members[i];
    auto found = at.find(member.host);
    if (found == at.end()) {
      found = at.emplace(member.host, groups.size()).first;
      groups.push_back(Group{});
      groups.back().host = member.host;
      groups.back().newestMember = i;
    }
    Group& group = groups[found->second];
    ++group.connections;
    group.byteCount += member.byteCount;
    group.packetCount += member.packetCount;
    group.latestMs = std::max(group.latestMs, member.timeMs);
    group.anyBlocked = group.anyBlocked || member.block;
    group.anyLocal = group.anyLocal || member.local;
  }
  std::stable_sort(groups.begin(), groups.end(),
                   [](const Group& a, const Group& b) { return a.latestMs > b.latestMs; });
  return groups;
}

// ---- the verdict ratio bar -------------------------------------------------

// The shares of the 3px bar under the Connections header, in its order:
// allowed (green), blocked (coral), bypassed (amber). Allowed and blocked are
// the session's BlockStats counts; the bypassed count is the cached window's,
// the only place a bypass count exists (the SDK keeps no session one), so the
// bar mixes the two scopes, as Windows' does.
struct Ratio {
  double allowed = 0;
  double blocked = 0;
  double bypassed = 0;
};

// nullopt when there is nothing to proportion; a negative count reads as 0.
// The sum is taken in double, so no count can overflow it.
inline std::optional<Ratio> VerdictRatio(int64_t allowed, int64_t blocked, int64_t bypassed) {
  const double a = static_cast<double>(std::max<int64_t>(0, allowed));
  const double b = static_cast<double>(std::max<int64_t>(0, blocked));
  const double l = static_cast<double>(std::max<int64_t>(0, bypassed));
  const double total = a + b + l;
  if (total <= 0) return std::nullopt;
  return Ratio{a / total, b / total, l / total};
}

}  // namespace urnw::connection_filter
