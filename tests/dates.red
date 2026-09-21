// Turning a number of seconds into something a person can read.
//
// Everything here uses UTC, because a test that depended on the machine's
// time zone would pass in one place and fail in another.

const start = date(0, true);
print(start["year"]);                     // expect: 1970
print(start["month"]);                    // expect: 1
print(start["day"]);                      // expect: 1
print(start["hour"]);                     // expect: 0
print(start["minute"]);                   // expect: 0
print(start["second"]);                   // expect: 0
print(start["weekday"]);                  // expect: 4
print(start["yearday"]);                  // expect: 1

// A moment with every field doing something: 2024-02-29T13:45:07Z, a
// leap day, which was a Thursday.
const leap = date(1709214307, true);
print(leap["year"], leap["month"], leap["day"]);    // expect: 2024 2 29
print(leap["hour"], leap["minute"], leap["second"]); // expect: 13 45 7
print(leap["weekday"]);                   // expect: 4
print(leap["yearday"]);                   // expect: 60

// format_time takes the strftime patterns, which are the ones already
// written down everywhere.
print(format_time(0, "%Y-%m-%d %H:%M:%S", true));   // expect: 1970-01-01 00:00:00
print(format_time(1709214307, "%Y-%m-%d", true));   // expect: 2024-02-29
print(format_time(1709214307, "%H:%M", true));      // expect: 13:45
print(format_time(1709214307, "no pattern here", true));  // expect: no pattern here

// date() with no arguments is now, which has to be after this was
// written and before it is unreasonable.
const now = date();
print(now["year"] >= 2024 and now["year"] < 2200);  // expect: true
print(now["month"] >= 1 and now["month"] <= 12);    // expect: true
print(now["day"] >= 1 and now["day"] <= 31);        // expect: true
print(abs(time() - 0) > 1600000000);                // expect: true
