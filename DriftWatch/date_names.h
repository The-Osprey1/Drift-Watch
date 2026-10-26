// Day and month names, in one place so a correction is made once. Two casings, because the
// two halves of the interface read differently: the faces are instrument-styled and use upper
// case, the settings and set time screens use title case. Kept free of display headers (like
// face_id.h) so any unit can include it.
#pragma once

namespace datenames {
// Indexed by tm_wday (0 = Sunday) and tm_mon (0 = January). constexpr at namespace scope, so
// each unit gets its own pointers and the literals are shared by the linker.
constexpr const char *const DAYS_UPPER[7] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
constexpr const char *const MONTHS_UPPER[12] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                                "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
constexpr const char *const DAYS_TITLE[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
constexpr const char *const MONTHS_TITLE[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                                "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
}  // namespace datenames
