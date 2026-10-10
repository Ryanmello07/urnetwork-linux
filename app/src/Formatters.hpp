// Formatting and validation helpers for the connect drawer (exact ports of the
// apple app's RateFormatUtils/HostFormatUtils, per the connect drawer spec).
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <cstdint>
#include <string>
#include <vector>

// FormatCountCompact and FormatCountRate, pure so the unit tests reach them
#include "CountFormat.hpp"

namespace urnw {

// IEC base-1024: "996 B", "1.2 KiB", "3.4 MiB", "1.1 GiB".
std::string FormatByteCountCompact(int64_t byteCount);
// "1.2 KiB/s"
std::string FormatByteRate(int64_t bytesPerSecond);
// "340 pkt/s"
std::string FormatPacketRate(int64_t packetsPerSecond);
// "1.2 Mbps"
std::string FormatBitRate(int64_t bitsPerSecond);

// "now", "42s ago", "3m ago", "2h ago", "4d ago", and from 7 days on the
// event's date in this machine's locale and zone (RelativeTimeSpan.hpp).
std::string RelativeTime(int64_t secondsAgo);

// A Unix second in this machine's zone: the locale's date ("%x"), and that
// date with the time ("%x, %R"), which a time's accessible description and
// tooltip carry. "" when the second cannot be represented.
std::string LocalDate(int64_t unixSeconds);
std::string LocalDateTime(int64_t unixSeconds);

// Valid IPv4 or IPv6 literal.
bool IsIpAddressValue(const std::string& value);
// Parses as a URL with scheme https and a non-empty host.
bool IsValidDohUrl(const std::string& value);

std::string TrimWhitespace(const std::string& value);

}  // namespace urnw
