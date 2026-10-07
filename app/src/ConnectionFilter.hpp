// What the connect page's activity list shows of the routing-decision feed:
// the verdict filter (All, Blocked, Tunnelled, Bypassed), the host/address
// search, and the header count that reads "N hosts of M" while either holds
// rows back. All of it is view-side over the feed the page already caches,
// so the filter and a push can never disagree about the feed.
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

#include <cstdint>
#include <string>
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

// A filter is holding rows back: the header reads "N hosts of M", and the
// header offers Clear.
inline bool FilterActive(Verdict verdict, const std::string& query) {
  return verdict != Verdict::All || !query.empty();
}

// The list, rather than the "no session" sentence, shows while connected and
// a row shows, or while a filter hides every row of a feed that has some: an
// empty list under the filter says what happened, and the sentence would
// contradict it.
inline bool ShowList(bool connected, bool anyRow, bool filterActive, int64_t total) {
  return connected && (anyRow || (filterActive && 0 < total));
}

// The header's count: `shown` hosts, and `of` the total while ofTotal holds.
struct Count {
  int64_t shown = 0;
  bool ofTotal = false;
  int64_t of = 0;
};

// `passed` decisions passed the filter out of `total` in the feed.
inline Count CountFor(Verdict verdict, const std::string& query, int64_t passed, int64_t total) {
  Count count;
  count.ofTotal = FilterActive(verdict, query);
  count.shown = count.ofTotal ? passed : total;
  count.of = total;
  return count;
}

}  // namespace urnw::connection_filter
