/*************************************************************************
 *
 * TIGHTDB CONFIDENTIAL
 * __________________
 *
 *  [2011] - [2012] TightDB Inc
 *  All Rights Reserved.
 *
 * NOTICE:  All information contained herein is, and remains
 * the property of TightDB Incorporated and its suppliers,
 * if any.  The intellectual and technical concepts contained
 * herein are proprietary to TightDB Incorporated
 * and its suppliers and may be covered by U.S. and Foreign Patents,
 * patents in process, and are protected by trade secret or copyright law.
 * Dissemination of this information or reproduction of this material
 * is strictly forbidden unless prior written permission is obtained
 * from TightDB Incorporated.
 *
 **************************************************************************/
#ifndef TIGHTDB_DATE_HPP
#define TIGHTDB_DATE_HPP

#include <ctime>
#include <ostream>

namespace tightdb {


class Date {
public:
    Date() TIGHTDB_NOEXCEPT: m_time(0) {}

    /// Construct from the number of seconds since Jan 1 00:00:00 UTC
    /// 1970.
    Date(std::time_t d) TIGHTDB_NOEXCEPT: m_time(d) {}

    /// Return the time as seconds since Jan 1 00:00:00 UTC 1970.
    std::time_t get_date() const TIGHTDB_NOEXCEPT { return m_time; }

    bool operator==(const Date& d) const TIGHTDB_NOEXCEPT { return m_time == d.m_time; }
    bool operator!=(const Date& d) const TIGHTDB_NOEXCEPT { return m_time != d.m_time; }

    /// Construct from broken down local time.
    ///
    /// \note This constructor uses std::mktime() to convert the
    /// specified local time to seconds since the Epoch, that is, the
    /// result depends on the current globally specified time zone
    /// setting.
    ///
    /// \param year The year (the minimum valid value is 1970).
    ///
    /// \param month The month in the range [1, 12].
    ///
    /// \param day The day of the month in the range [1, 31].
    ///
    /// \param hours Hours since midnight in the range [0, 23].
    ///
    /// \param minutes Minutes after the hour in the range [0, 59].
    ///
    /// \param seconds Seconds after the minute in the range [0,
    /// 60]. Note that the range allows for leap seconds.
    Date(int year, int month, int day, int hours = 0, int minutes = 0, int seconds = 0):
        m_time(assemble(year, month, day, hours, minutes, seconds)) {}

    template<class Ch, class Tr>
    friend std::basic_ostream<Ch, Tr>& operator<<(std::basic_ostream<Ch, Tr>& out, const Date& d)
    {
        out << "Date("<<d.m_time<<")";
        return out;
    }

private:
    std::time_t m_time; // Seconds since Jan 1 00:00:00 UTC 1970.

    static std::time_t assemble(int year, int month, int day, int hours, int minutes, int seconds)
    {
        std::tm local_time;
        local_time.tm_year  = year  - 1900;
        local_time.tm_mon   = month - 1;
        local_time.tm_mday  = day;
        local_time.tm_hour  = hours;
        local_time.tm_min   = minutes;
        local_time.tm_sec   = seconds;
        local_time.tm_isdst = -1;
        return std::mktime(&local_time);
    }
};


} // namespace tightdb

#endif // TIGHTDB_DATE_HPP

