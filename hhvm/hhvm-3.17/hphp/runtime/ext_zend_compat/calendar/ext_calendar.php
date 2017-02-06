<?hh
// generated by idl-to-hni.php

/* This function will return the number of days in the month of year for the
 * specified calendar.
 */
<<__Native("ZendCompat")>>
function cal_days_in_month(mixed $calendar,
                           mixed $month,
                           mixed $year): mixed;

/* cal_from_jd() converts the Julian day given in jd into a date of the
 * specified calendar. Supported calendar values are CAL_GREGORIAN,
 * CAL_JULIAN, CAL_JEWISH and CAL_FRENCH.
 */
<<__Native("ZendCompat")>>
function cal_from_jd(mixed $jd,
                     mixed $calendar): mixed;

/* cal_info() returns information on the specified calendar.  Calendar
 * information is returned as an array containing the elements calname,
 * calsymbol, month, abbrevmonth and maxdaysinmonth. The names of the
 * different calendars which can be used as calendar are as follows: 0 or
 * CAL_GREGORIAN - Gregorian Calendar 1 or CAL_JULIAN - Julian Calendar 2 or
 * CAL_JEWISH - Jewish Calendar 3 or CAL_FRENCH - French Revolutionary
 * Calendar  If no calendar is specified information on all supported
 * calendars is returned as an array.
 */
<<__Native("ZendCompat")>>
function cal_info(mixed $calendar): mixed;

/* cal_to_jd() calculates the Julian day count for a date in the specified
 * calendar. Supported calendars are CAL_GREGORIAN, CAL_JULIAN, CAL_JEWISH and
 * CAL_FRENCH.
 */
<<__Native("ZendCompat")>>
function cal_to_jd(mixed $calendar,
                   mixed $month,
                   mixed $day,
                   mixed $year): mixed;

/* Returns the Unix timestamp corresponding to midnight on Easter of the given
 * year. Warning  This function will generate a warning if the year is outside
 * of the range for Unix timestamps (i.e. before 1970 or after 2037).  The
 * date of Easter Day was defined by the Council of Nicaea in AD325 as the
 * Sunday after the first full moon which falls on or after the Spring
 * Equinox. The Equinox is assumed to always fall on 21st March, so the
 * calculation reduces to determining the date of the full moon and the date
 * of the following Sunday. The algorithm used here was introduced around the
 * year 532 by Dionysius Exiguus. Under the Julian Calendar (for years before
 * 1753) a simple 19-year cycle is used to track the phases of the Moon. Under
 * the Gregorian Calendar (for years after 1753 - devised by Clavius and
 * Lilius, and introduced by Pope Gregory XIII in October 1582, and into
 * Britain and its then colonies in September 1752) two correction factors are
 * added to make the cycle more accurate.
 */
<<__Native("ZendCompat")>>
function easter_date(mixed $year): mixed;

/* Returns the number of days after March 21 on which Easter falls for a given
 * year. If no year is specified, the current year is assumed.  This function
 * can be used instead of easter_date() to calculate Easter for years which
 * fall outside the range of Unix timestamps (i.e. before 1970 or after 2037).
 *  The date of Easter Day was defined by the Council of Nicaea in AD325 as
 * the Sunday after the first full moon which falls on or after the Spring
 * Equinox. The Equinox is assumed to always fall on 21st March, so the
 * calculation reduces to determining the date of the full moon and the date
 * of the following Sunday. The algorithm used here was introduced around the
 * year 532 by Dionysius Exiguus. Under the Julian Calendar (for years before
 * 1753) a simple 19-year cycle is used to track the phases of the Moon. Under
 * the Gregorian Calendar (for years after 1753 - devised by Clavius and
 * Lilius, and introduced by Pope Gregory XIII in October 1582, and into
 * Britain and its then colonies in September 1752) two correction factors are
 * added to make the cycle more accurate.
 */
<<__Native("ZendCompat")>>
function easter_days(mixed $year,
                     mixed $method): mixed;

/* Converts a date from the French Republican Calendar to a Julian Day Count.
 * These routines only convert dates in years 1 through 14 (Gregorian dates 22
 * September 1792 through 22 September 1806). This more than covers the period
 * when the calendar was in use.
 */
<<__Native("ZendCompat")>>
function frenchtojd(mixed $month,
                    mixed $day,
                    mixed $year): mixed;

/* Valid Range for Gregorian Calendar 4714 B.C. to 9999 A.D.  Although this
 * function can handle dates all the way back to 4714 B.C., such use may not
 * be meaningful. The Gregorian calendar was not instituted until October 15,
 * 1582 (or October 5, 1582 in the Julian calendar). Some countries did not
 * accept it until much later. For example, Britain converted in 1752, The
 * USSR in 1918 and Greece in 1923. Most European countries used the Julian
 * calendar prior to the Gregorian.
 */
<<__Native("ZendCompat")>>
function gregoriantojd(mixed $month,
                       mixed $day,
                       mixed $year): mixed;

/* Returns the day of the week. Can return a string or an integer depending on
 * the mode.
 */
<<__Native("ZendCompat")>>
function jddayofweek(mixed $julianday,
                     mixed $mode): mixed;

/* Returns a string containing a month name. mode tells this function which
 * calendar to convert the Julian Day Count to, and what type of month names
 * are to be returned. Calendar modes Mode Meaning Values 0 Gregorian -
 * abbreviated Jan, Feb, Mar, Apr, May, Jun, Jul, Aug, Sep, Oct, Nov, Dec 1
 * Gregorian January, February, March, April, May, June, July, August,
 * September, October, November, December 2 Julian - abbreviated Jan, Feb,
 * Mar, Apr, May, Jun, Jul, Aug, Sep, Oct, Nov, Dec 3 Julian January,
 * February, March, April, May, June, July, August, September, October,
 * November, December 4 Jewish Tishri, Heshvan, Kislev, Tevet, Shevat, AdarI,
 * AdarII, Nisan, Iyyar, Sivan, Tammuz, Av, Elul 5 French Republican
 * Vendemiaire, Brumaire, Frimaire, Nivose, Pluviose, Ventose, Germinal,
 * Floreal, Prairial, Messidor, Thermidor, Fructidor, Extra
 */
<<__Native("ZendCompat")>>
function jdmonthname(mixed $jday,
                     mixed $calendar): mixed;

/* Converts a Julian Day Count to the French Republican Calendar.
 */
<<__Native("ZendCompat")>>
function jdtofrench(mixed $julianday): mixed;

/* Converts Julian Day Count to a string containing the Gregorian date in the
 * format of "month/day/year".
 */
<<__Native("ZendCompat")>>
function jdtogregorian(mixed $julianday): mixed;

/* Converts a Julian Day Count to the Jewish Calendar.
 */
<<__Native("ZendCompat")>>
function jdtojewish(mixed $julianday,
                    mixed $hebrew,
                    mixed $fl): mixed;

/* Converts Julian Day Count to a string containing the Julian Calendar Date
 * in the format of "month/day/year".
 */
<<__Native("ZendCompat")>>
function jdtojulian(mixed $julianday): mixed;

/* This function will return a Unix timestamp corresponding to the Julian Day
 * given in jday or FALSE if jday is not inside the Unix epoch (Gregorian
 * years between 1970 and 2037 or 2440588 <= jday <= 2465342 ). The time
 * returned is localtime (and not GMT).
 */
<<__Native("ZendCompat")>>
function jdtounix(mixed $jday): mixed;

/* Although this function can handle dates all the way back to the year 1
 * (3761 B.C.), such use may not be meaningful. The Jewish calendar has been
 * in use for several thousand years, but in the early days there was no
 * formula to determine the start of a month. A new month was started when the
 * new moon was first observed.
 */
<<__Native("ZendCompat")>>
function jewishtojd(mixed $month,
                    mixed $day,
                    mixed $year): mixed;

/* Valid Range for Julian Calendar 4713 B.C. to 9999 A.D.  Although this
 * function can handle dates all the way back to 4713 B.C., such use may not
 * be meaningful. The calendar was created in 46 B.C., but the details did not
 * stabilize until at least 8 A.D., and perhaps as late at the 4th century.
 * Also, the beginning of a year varied from one culture to another - not all
 * accepted January as the first month. Caution  Remember, the current
 * calendar system being used worldwide is the Gregorian calendar.
 * gregoriantojd() can be used to convert such dates to their Julian Day
 * count.
 */
<<__Native("ZendCompat")>>
function juliantojd(mixed $month,
                    mixed $day,
                    mixed $year): mixed;

/* Return the Julian Day for a Unix timestamp (seconds since 1.1.1970), or for
 * the current day if no timestamp is given.
 */
<<__Native("ZendCompat")>>
function unixtojd(mixed $timestamp): mixed;