// What the connect page's activity list shows of the feed (ConnectionFilter.hpp):
// the verdict filter, the host/address search, the "N hosts of M" count and
// when an empty list stands in for the "no session" sentence.
// SPDX-License-Identifier: MPL-2.0
#include "TestHarness.hpp"

#include <string>
#include <vector>

#include "ConnectionFilter.hpp"

using urnw::connection_filter::CountFor;
using urnw::connection_filter::Decision;
using urnw::connection_filter::FilterActive;
using urnw::connection_filter::NormalizeQuery;
using urnw::connection_filter::Passes;
using urnw::connection_filter::QueryPasses;
using urnw::connection_filter::ShowList;
using urnw::connection_filter::Verdict;
using urnw::connection_filter::VerdictPasses;

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
  const auto plain = CountFor(Verdict::All, "", 30, 30);
  UR_EXPECT_FALSE(plain.ofTotal);
  UR_EXPECT_EQ(30, plain.shown);
  const auto filtered = CountFor(Verdict::Blocked, "", 4, 30);
  UR_EXPECT_TRUE(filtered.ofTotal);
  UR_EXPECT_EQ(4, filtered.shown);
  UR_EXPECT_EQ(30, filtered.of);
  const auto none = CountFor(Verdict::All, "nothing", 0, 30);
  UR_EXPECT_TRUE(none.ofTotal);
  UR_EXPECT_EQ(0, none.shown);
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
