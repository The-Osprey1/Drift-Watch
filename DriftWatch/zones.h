// Summer time rules for the time zones the Auto lookup can name. The lookup service only gives
// the name and today's offset; a fixed offset would stay on summer or winter time until the next
// sync. With the zone's rules the clock changes by itself at the right moment.
//
// Only zones that change their clocks are listed: for every other zone the fixed offset is
// already right all year. A few zones whose rules the watch's C library cannot read (changes at
// hours past 24, or rules tied to religious calendars) are left out on purpose and keep the
// fixed offset. net.cpp also checks a rule gives the same offset as the lookup before using it,
// so a rule that has gone out of date can never move the clock.
//
// The rules are the POSIX strings from the IANA time zone database (release 2026e), checked by
// tools/host/net_test.cpp. Manitoba, Alberta and British Columbia have since stopped changing
// their clocks, so they are not listed.
#pragma once
#include <string.h>

namespace zones {

struct Rule {
  const char *name, *posix;
};

constexpr const char *EU_WEST = "WET0WEST,M3.5.0/1,M10.5.0";
constexpr const char *EU_CENTRAL = "CET-1CEST,M3.5.0,M10.5.0/3";
constexpr const char *EU_EAST = "EET-2EEST,M3.5.0/3,M10.5.0/4";
constexpr const char *US_EAST = "EST5EDT,M3.2.0,M11.1.0";
constexpr const char *US_CENTRAL = "CST6CDT,M3.2.0,M11.1.0";
constexpr const char *US_MOUNTAIN = "MST7MDT,M3.2.0,M11.1.0";
constexpr const char *US_PACIFIC = "PST8PDT,M3.2.0,M11.1.0";
constexpr const char *US_ALASKA = "AKST9AKDT,M3.2.0,M11.1.0";
constexpr const char *CA_ATLANTIC = "AST4ADT,M3.2.0,M11.1.0";
constexpr const char *AU_EAST = "AEST-10AEDT,M10.1.0,M4.1.0/3";
constexpr const char *AU_CENTRAL = "ACST-9:30ACDT,M10.1.0,M4.1.0/3";

constexpr Rule RULES[] = {
    // Europe
    {"Europe/London", "GMT0BST,M3.5.0/1,M10.5.0"},
    {"Europe/Jersey", "GMT0BST,M3.5.0/1,M10.5.0"},
    {"Europe/Guernsey", "GMT0BST,M3.5.0/1,M10.5.0"},
    {"Europe/Isle_of_Man", "GMT0BST,M3.5.0/1,M10.5.0"},
    {"Europe/Dublin", "GMT0IST,M3.5.0/1,M10.5.0"},  // same clock as the database's reversed form
    {"Europe/Lisbon", EU_WEST},
    {"Atlantic/Madeira", EU_WEST},
    {"Atlantic/Canary", EU_WEST},
    {"Atlantic/Faroe", EU_WEST},
    {"Atlantic/Azores", "<-01>1<+00>,M3.5.0/0,M10.5.0/1"},
    {"Europe/Amsterdam", EU_CENTRAL},
    {"Europe/Andorra", EU_CENTRAL},
    {"Europe/Belgrade", EU_CENTRAL},
    {"Europe/Berlin", EU_CENTRAL},
    {"Europe/Bratislava", EU_CENTRAL},
    {"Europe/Brussels", EU_CENTRAL},
    {"Europe/Budapest", EU_CENTRAL},
    {"Europe/Busingen", EU_CENTRAL},
    {"Europe/Copenhagen", EU_CENTRAL},
    {"Europe/Gibraltar", EU_CENTRAL},
    {"Europe/Ljubljana", EU_CENTRAL},
    {"Europe/Luxembourg", EU_CENTRAL},
    {"Europe/Madrid", EU_CENTRAL},
    {"Europe/Malta", EU_CENTRAL},
    {"Europe/Monaco", EU_CENTRAL},
    {"Europe/Oslo", EU_CENTRAL},
    {"Europe/Paris", EU_CENTRAL},
    {"Europe/Podgorica", EU_CENTRAL},
    {"Europe/Prague", EU_CENTRAL},
    {"Europe/Rome", EU_CENTRAL},
    {"Europe/San_Marino", EU_CENTRAL},
    {"Europe/Sarajevo", EU_CENTRAL},
    {"Europe/Skopje", EU_CENTRAL},
    {"Europe/Stockholm", EU_CENTRAL},
    {"Europe/Tirane", EU_CENTRAL},
    {"Europe/Vaduz", EU_CENTRAL},
    {"Europe/Vatican", EU_CENTRAL},
    {"Europe/Vienna", EU_CENTRAL},
    {"Europe/Warsaw", EU_CENTRAL},
    {"Europe/Zagreb", EU_CENTRAL},
    {"Europe/Zurich", EU_CENTRAL},
    {"Africa/Ceuta", EU_CENTRAL},
    {"Arctic/Longyearbyen", EU_CENTRAL},
    {"Europe/Athens", EU_EAST},
    {"Europe/Bucharest", EU_EAST},
    {"Europe/Helsinki", EU_EAST},
    {"Europe/Kiev", EU_EAST},
    {"Europe/Kyiv", EU_EAST},  // "Kiev" is the older spelling some services still send
    {"Europe/Mariehamn", EU_EAST},
    {"Europe/Riga", EU_EAST},
    {"Europe/Sofia", EU_EAST},
    {"Europe/Tallinn", EU_EAST},
    {"Europe/Vilnius", EU_EAST},
    {"Asia/Nicosia", EU_EAST},
    {"Asia/Famagusta", EU_EAST},
    {"Europe/Chisinau", EU_EAST},
    {"Asia/Beirut", "EET-2EEST,M3.5.0/0,M10.5.0/0"},
    // North America
    {"America/New_York", US_EAST},
    {"America/Detroit", US_EAST},
    {"America/Toronto", US_EAST},
    {"America/Montreal", US_EAST},
    {"America/Nassau", US_EAST},
    {"America/Iqaluit", US_EAST},
    {"America/Indiana/Indianapolis", US_EAST},
    {"America/Kentucky/Louisville", US_EAST},
    {"America/Port-au-Prince", US_EAST},
    {"America/Grand_Turk", US_EAST},
    {"America/Chicago", US_CENTRAL},
    {"America/Menominee", US_CENTRAL},
    {"America/Indiana/Knox", US_CENTRAL},
    {"America/North_Dakota/Center", US_CENTRAL},
    {"America/Matamoros", US_CENTRAL},
    {"America/Ojinaga", US_CENTRAL},
    {"America/Rankin_Inlet", US_CENTRAL},
    {"America/Denver", US_MOUNTAIN},
    {"America/Boise", US_MOUNTAIN},
    {"America/Ciudad_Juarez", US_MOUNTAIN},
    {"America/Los_Angeles", US_PACIFIC},
    {"America/Tijuana", US_PACIFIC},
    {"America/Anchorage", US_ALASKA},
    {"America/Juneau", US_ALASKA},
    {"America/Sitka", US_ALASKA},
    {"America/Nome", US_ALASKA},
    {"America/Yakutat", US_ALASKA},
    {"America/Adak", "HST10HDT,M3.2.0,M11.1.0"},
    {"America/Halifax", CA_ATLANTIC},
    {"America/Moncton", CA_ATLANTIC},
    {"America/Glace_Bay", CA_ATLANTIC},
    {"America/Goose_Bay", CA_ATLANTIC},
    {"America/Thule", CA_ATLANTIC},
    {"Atlantic/Bermuda", CA_ATLANTIC},
    {"America/St_Johns", "NST3:30NDT,M3.2.0,M11.1.0"},
    {"America/Havana", "CST5CDT,M3.2.0/0,M11.1.0/1"},
    {"America/Miquelon", "<-03>3<-02>,M3.2.0,M11.1.0"},
    // Southern hemisphere
    {"Australia/Sydney", AU_EAST},
    {"Australia/Melbourne", AU_EAST},
    {"Australia/Hobart", AU_EAST},
    {"Australia/Canberra", AU_EAST},
    {"Antarctica/Macquarie", AU_EAST},
    {"Australia/Adelaide", AU_CENTRAL},
    {"Australia/Broken_Hill", AU_CENTRAL},
    {"Australia/Lord_Howe", "<+1030>-10:30<+11>-11,M10.1.0,M4.1.0"},
    {"Pacific/Auckland", "NZST-12NZDT,M9.5.0,M4.1.0/3"},
    {"Antarctica/McMurdo", "NZST-12NZDT,M9.5.0,M4.1.0/3"},
    {"Pacific/Chatham", "<+1245>-12:45<+1345>,M9.5.0/2:45,M4.1.0/3:45"},
    {"Pacific/Norfolk", "<+11>-11<+12>,M10.1.0,M4.1.0/3"},
};

// The rules for an IANA zone name such as "Europe/London", or nullptr if it is not listed.
inline const char *rulesFor(const char *name) {
  if (!name || !*name) return nullptr;
  for (const Rule &r : RULES)
    if (!strcmp(r.name, name)) return r.posix;
  return nullptr;
}

}  // namespace zones
