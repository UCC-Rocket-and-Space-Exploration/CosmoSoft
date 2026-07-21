#ifndef COSMO_SOFT_EVENTLOGFORMATTING_H
#define COSMO_SOFT_EVENTLOGFORMATTING_H

#include <QString>

namespace cosmo::event_log_detail {

/** Maximum retained UTF-16 code units for one event-log message. */
inline constexpr qsizetype kMaxEntryCharacters = 4096;

/**
 * @brief Returns the visible suffix used when an event-log message is clipped.
 */
[[nodiscard]] inline QString truncationMarker()
{
    return QStringLiteral(" \u2026 [truncated]");
}

/**
 * @brief Normalises a message to one bounded document block.
 *
 * Line and paragraph separators are replaced with spaces before the text is
 * clipped. The returned string never exceeds kMaxEntryCharacters UTF-16 code
 * units and truncation never splits a surrogate pair.
 */
[[nodiscard]] inline QString boundedSingleLineText(const QString &text)
{
    QString result;
    result.reserve(kMaxEntryCharacters + 1);
    qsizetype sourceIndex = 0;
    while (sourceIndex < text.size() && result.size() <= kMaxEntryCharacters) {
        QChar character = text.at(sourceIndex);
        ++sourceIndex;
        if (character == u'\r') {
            if (sourceIndex < text.size() && text.at(sourceIndex) == u'\n') {
                ++sourceIndex;
            }
            character = u' ';
        } else if (character == u'\n'
                   || character == u'\v'
                   || character == u'\f'
                   || character == u'\u0085'
                   || character == QChar::LineSeparator
                   || character == QChar::ParagraphSeparator) {
            character = u' ';
        }
        result.append(character);
    }

    const bool truncated = sourceIndex < text.size()
        || result.size() > kMaxEntryCharacters;
    if (!truncated) {
        return result;
    }

    const QString marker = truncationMarker();
    qsizetype retainedCharacters = kMaxEntryCharacters - marker.size();
    if (retainedCharacters > 0
        && result.at(retainedCharacters - 1).isHighSurrogate()
        && result.at(retainedCharacters).isLowSurrogate()) {
        --retainedCharacters;
    }
    result.truncate(retainedCharacters);
    result.append(marker);
    return result;
}

} // namespace cosmo::event_log_detail

#endif // COSMO_SOFT_EVENTLOGFORMATTING_H
