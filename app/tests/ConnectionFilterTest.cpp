// What the connect page's activity list shows of the feed (ConnectionFilter.hpp):
// the verdict filter, the host/address search, the group-by-host fold, the
// "N hosts of M" count, when the header offers Clear, and when an empty list
// stands in for the "no session" sentence, and the verdict ratio bar's shares.
// SPDX-License-Identifier: MPL-2.0
#include "TestHarness.hpp"

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "ConnectionFilter.hpp"

using urnw::connection_filter::ClearOffered;
using urnw::connection_filter::CountFor;
using urnw::connection_filter::Decision;
using urnw::connection_filter::FilterActive;
using urnw::connection_filter::FoldGroups;
using urnw::connection_filter::FoldMember;
using urnw::connection_filter::Group;
using urnw::connection_filter::NormalizeQuery;
using urnw::connection_filter::Passes;
using urnw::connection_filter::QueryPasses;
using urnw::connection_filter::ShowList;
using urnw::connection_filter::Verdict;
using urnw::connection_filter::VerdictPasses;
using urnw::connection_filter::VerdictRatio;

// Each verdict passes exactly its decisions; All passes every one.
UR_TEST(ConnectionFilter_VerdictsSplitTheFeedThreeWays) {
  struct Case {
    bool block;
    bool local;
  };
  for (const Case c : {Case{true, false}, Case{true, true}, Case{false, false}, Case{false, true}}) {
    UR_EXPECT_TRUE(VerdictPasses(Verdict::All, c.block, c.local));
    UR_EXPECT_EQ(c.block, VerdictPasses(Verdict::Blocked, c.block, c.local));
    UR_EXPECT_EQ(!c.block && !c.local, VerdictPasses(Verdict::Tunnelled, c.block, c.local));
    UR_EXPECT_EQ(!c.block && c.local, VerdictPasses(Verdict::Bypassed, c.block, c.local));
  }
}

UR_TEST(ConnectionFilter_TheQueryIsTrimmedAndLowercased) {
  UR_EXPECT_TRUE(NormalizeQuery("  Mail.Example.COM \t") == "mail.example.com");
  UR_EXPECT_TRUE(NormalizeQuery("   ").empty());
  UR_EXPECT_TRUE(NormalizeQuery("").empty());
  UR_EXPECT_TRUE(NormalizeQuery("2001:DB8::1") == "2001:db8::1");
}

// The search matches a substring of any of the four lists, in any case.
UR_TEST(ConnectionFilter_TheQueryMatchesEveryIdentityList) {
  const std::vector<std::string> hosts = {"cdn.example.net"};
  const std::vector<std::string> ips = {"192.0.2.10"};
  const std::vector<std::string> matchedHosts = {"Video.Example.ORG"};
  const std::vector<std::string> matchedIps = {"2001:db8::7"};
  Decision decision;
  decision.hosts = &hosts;
  decision.ips = &ips;
  decision.matchedHosts = &matchedHosts;
  decision.matchedIps = &matchedIps;
  UR_EXPECT_TRUE(QueryPasses("", decision));
  UR_EXPECT_TRUE(QueryPasses("cdn.example", decision));
  UR_EXPECT_TRUE(QueryPasses("192.0.2", decision));
  UR_EXPECT_TRUE(QueryPasses("video.example.org", decision));
  UR_EXPECT_TRUE(QueryPasses(NormalizeQuery(" VIDEO "), decision));
  UR_EXPECT_TRUE(QueryPasses("db8::7", decision));
  UR_EXPECT_FALSE(QueryPasses("198.51.100", decision));
  UR_EXPECT_FALSE(QueryPasses("mail", decision));
  // a decision the feed sent no lists for matches only the empty query
  UR_EXPECT_TRUE(QueryPasses("", Decision{}));
  UR_EXPECT_FALSE(QueryPasses("a", Decision{}));
}

UR_TEST(ConnectionFilter_BothFiltersMustPass) {
  const std::vector<std::string> hosts = {"cdn.example.net"};
  Decision blocked;
  blocked.hosts = &hosts;
  blocked.block = true;
  UR_EXPECT_TRUE(Passes(Verdict::All, "cdn", blocked));
  UR_EXPECT_TRUE(Passes(Verdict::Blocked, "cdn", blocked));
  UR_EXPECT_FALSE(Passes(Verdict::Tunnelled, "cdn", blocked));
  UR_EXPECT_FALSE(Passes(Verdict::Blocked, "mail", blocked));
}

// "N hosts" with no filter, "N hosts of M" while one holds rows back.
UR_TEST(ConnectionFilter_TheCountReadsOfTheTotalWhileFiltered) {
  UR_EXPECT_FALSE(FilterActive(Verdict::All, ""));
  UR_EXPECT_TRUE(FilterActive(Verdict::Bypassed, ""));
  UR_EXPECT_TRUE(FilterActive(Verdict::All, "cdn"));
  const auto plain = CountFor(Verdict::All, "", false, 0, 30, 30);
  UR_EXPECT_FALSE(plain.ofTotal);
  UR_EXPECT_EQ(30, plain.shown);
  const auto filtered = CountFor(Verdict::Blocked, "", false, 0, 4, 30);
  UR_EXPECT_TRUE(filtered.ofTotal);
  UR_EXPECT_EQ(4, filtered.shown);
  UR_EXPECT_EQ(30, filtered.of);
  const auto none = CountFor(Verdict::All, "nothing", false, 0, 0, 30);
  UR_EXPECT_TRUE(none.ofTotal);
  UR_EXPECT_EQ(0, none.shown);
}

// Folded, the count is the hosts of the decisions that passed: "8 hosts of
// 30", and "of" the filtered decisions while a filter holds some back.
UR_TEST(ConnectionFilter_TheFoldCountsHostsOfTheDecisions) {
  const auto grouped = CountFor(Verdict::All, "", true, 8, 30, 30);
  UR_EXPECT_TRUE(grouped.ofTotal);
  UR_EXPECT_EQ(8, grouped.shown);
  UR_EXPECT_EQ(30, grouped.of);
  const auto both = CountFor(Verdict::Blocked, "", true, 2, 5, 30);
  UR_EXPECT_EQ(2, both.shown);
  UR_EXPECT_EQ(5, both.of);
}

// Clear is offered while a filter, the search or the fold changes the list.
UR_TEST(ConnectionFilter_ClearIsOfferedOffTheDefaults) {
  UR_EXPECT_FALSE(ClearOffered(Verdict::All, "", false));
  UR_EXPECT_TRUE(ClearOffered(Verdict::Tunnelled, "", false));
  UR_EXPECT_TRUE(ClearOffered(Verdict::All, "cdn", false));
  UR_EXPECT_TRUE(ClearOffered(Verdict::All, "", true));
}

namespace {

FoldMember Member(const std::string& host, int64_t timeMs, int64_t bytes, int64_t packets,
                  bool block = false, bool local = false) {
  FoldMember member;
  member.host = host;
  member.timeMs = timeMs;
  member.byteCount = bytes;
  member.packetCount = packets;
  member.block = block;
  member.local = local;
  return member;
}

}  // namespace

// The fold sums each host's decisions and keeps its latest time.
UR_TEST(ConnectionFilter_TheFoldSumsEachHost) {
  const auto groups = FoldGroups({Member("a.example", 300, 10, 1), Member("b.example", 250, 5, 2),
                                  Member("a.example", 200, 20, 3), Member("a.example", 100, 40, 4)});
  UR_EXPECT_EQ(2, static_cast<int>(groups.size()));
  const Group& a = groups[0];
  UR_EXPECT_TRUE(a.host == "a.example");
  UR_EXPECT_EQ(3, a.connections);
  UR_EXPECT_EQ(70, a.byteCount);
  UR_EXPECT_EQ(8, a.packetCount);
  UR_EXPECT_EQ(300, a.latestMs);
  UR_EXPECT_EQ(0, static_cast<int>(a.newestMember));
  UR_EXPECT_TRUE(groups[1].host == "b.example");
  UR_EXPECT_EQ(1, groups[1].connections);
  UR_EXPECT_EQ(1, static_cast<int>(groups[1].newestMember));
  UR_EXPECT_TRUE(FoldGroups({}).empty());
}

// Blocked wins over bypassed, which wins over tunnelled.
UR_TEST(ConnectionFilter_TheFoldVerdictIsThePrecedence) {
  const auto groups = FoldGroups({Member("t.example", 9, 1, 1),
                                  Member("l.example", 8, 1, 1, false, true),
                                  Member("l.example", 7, 1, 1),
                                  Member("x.example", 6, 1, 1, false, true),
                                  Member("x.example", 5, 1, 1, true),
                                  Member("x.example", 4, 1, 1)});
  UR_EXPECT_EQ(3, static_cast<int>(groups.size()));
  UR_EXPECT_FALSE(groups[0].blocked());
  UR_EXPECT_FALSE(groups[0].bypassed());
  UR_EXPECT_FALSE(groups[1].blocked());
  UR_EXPECT_TRUE(groups[1].bypassed());
  UR_EXPECT_TRUE(groups[2].blocked());
  UR_EXPECT_FALSE(groups[2].bypassed());
}

// Groups read latest decision first, whatever order the hosts first appear
// in; a tie keeps the feed's order.
UR_TEST(ConnectionFilter_TheFoldSortsByTheLatestDecision) {
  const auto groups = FoldGroups({Member("old.example", 100, 1, 1), Member("new.example", 500, 1, 1),
                                  Member("old.example", 50, 1, 1), Member("tie.example", 100, 1, 1)});
  UR_EXPECT_EQ(3, static_cast<int>(groups.size()));
  UR_EXPECT_TRUE(groups[0].host == "new.example");
  UR_EXPECT_TRUE(groups[1].host == "old.example");
  UR_EXPECT_TRUE(groups[2].host == "tie.example");
}

// A filter that matches nothing in a session with rows keeps the (empty) list
// up; with no session, or no rows at all, the sentence shows.
UR_TEST(ConnectionFilter_AnEmptyFilteredListIsNotAnEmptySession) {
  UR_EXPECT_TRUE(ShowList(true, true, false, 3));
  UR_EXPECT_TRUE(ShowList(true, false, true, 3));
  UR_EXPECT_FALSE(ShowList(true, false, false, 0));
  UR_EXPECT_FALSE(ShowList(true, false, true, 0));
  UR_EXPECT_FALSE(ShowList(false, true, false, 3));
  UR_EXPECT_FALSE(ShowList(false, false, true, 3));
}

// Nothing to proportion collapses the bar; otherwise the shares sum to one.
UR_TEST(ConnectionFilter_TheRatioBarProportionsTheThreeVerdicts) {
  UR_EXPECT_FALSE(VerdictRatio(0, 0, 0).has_value());
  UR_EXPECT_FALSE(VerdictRatio(-3, 0, -1).has_value());
  const auto ratio = VerdictRatio(6, 3, 1);
  UR_EXPECT_TRUE(ratio.has_value());
  UR_EXPECT_NEAR(0.6, ratio->allowed, 1e-12);
  UR_EXPECT_NEAR(0.3, ratio->blocked, 1e-12);
  UR_EXPECT_NEAR(0.1, ratio->bypassed, 1e-12);
  UR_EXPECT_NEAR(1.0, ratio->allowed + ratio->blocked + ratio->bypassed, 1e-12);
  const auto local = VerdictRatio(0, 0, 4);
  UR_EXPECT_TRUE(local.has_value());
  UR_EXPECT_NEAR(0.0, local->allowed, 1e-12);
  UR_EXPECT_NEAR(0.0, local->blocked, 1e-12);
  UR_EXPECT_NEAR(1.0, local->bypassed, 1e-12);
  // counts near the int64 limit do not overflow the sum
  const int64_t huge = std::numeric_limits<int64_t>::max();
  const auto big = VerdictRatio(huge, huge, 0);
  UR_EXPECT_TRUE(big.has_value());
  UR_EXPECT_NEAR(0.5, big->allowed, 1e-12);
  UR_EXPECT_NEAR(0.5, big->blocked, 1e-12);
}
