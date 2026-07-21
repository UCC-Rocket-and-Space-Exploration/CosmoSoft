#include <catch2/catch_test_macros.hpp>

#include "../src/gui/pages/EventLogFormatting.h"

#include <QString>

using namespace Qt::StringLiterals;

TEST_CASE("Event log formatting keeps each entry in one block", "[gui][event-log]")
{
    const QString input =
        u"one\r\ntwo\rthree\nfour\u2028five\u2029six\vseven\feight\u0085nine"_s;

    REQUIRE(cosmo::event_log_detail::boundedSingleLineText(input)
            == u"one two three four five six seven eight nine"_s);
}

TEST_CASE("Event log formatting caps retained message text", "[gui][event-log]")
{
    using namespace cosmo::event_log_detail;

    const QString marker = truncationMarker();
    const QString exact(kMaxEntryCharacters, u'x');
    REQUIRE(boundedSingleLineText(exact) == exact);

    const QString oversized(kMaxEntryCharacters + 100, u'x');
    const QString bounded = boundedSingleLineText(oversized);
    REQUIRE(bounded.size() == kMaxEntryCharacters);
    REQUIRE(bounded.endsWith(marker));
    REQUIRE_FALSE(bounded.contains(u'\n'));
    REQUIRE_FALSE(bounded.contains(u'\r'));
}

TEST_CASE("Event log formatting normalizes before applying its cap", "[gui][event-log]")
{
    using namespace cosmo::event_log_detail;

    QString input;
    input.reserve(5000);
    for (int index = 0; index < 2500; ++index) {
        input.append(u"\r\n"_s);
    }

    const QString bounded = boundedSingleLineText(input);
    REQUIRE(bounded == QString(2500, u' '));
    REQUIRE_FALSE(bounded.endsWith(truncationMarker()));
}

TEST_CASE("Event log truncation does not split a surrogate pair", "[gui][event-log]")
{
    using namespace cosmo::event_log_detail;

    const qsizetype contentLimit = kMaxEntryCharacters - truncationMarker().size();
    QString input(contentLimit - 1, u'x');
    input.append(QString::fromUtf8("\xF0\x9F\x9A\x80"));
    input.append(QString(100, u'y'));

    const QString bounded = boundedSingleLineText(input);
    REQUIRE(bounded.size() <= kMaxEntryCharacters);
    REQUIRE(bounded.endsWith(truncationMarker()));
    REQUIRE_FALSE(bounded.at(contentLimit - 1).isLowSurrogate());
    REQUIRE_FALSE(bounded.at(contentLimit - 2).isHighSurrogate());
}
