//
// Created by Luna Nothard on 27/12/2021.
//

#ifndef WARZONE2100_CONSOLE_H
#define WARZONE2100_CONSOLE_H

#include <string>

/** How the text is aligned in the console window.
 *  Left-alignment by default
 */
enum class TEXT_JUSTIFICATION
{
    LEFT,
    RIGHT,
    CENTRE,
    DEFAULT = LEFT
};

///
struct ConsoleMessage
{
    /// Actual mess
    std::string text;

    /// Formatting
    TEXT_JUSTIFICATION justification;

    /** The unique id belonging to the player
     *  who issued this message
     */
    unsigned sender;

    /** The length of time the message will be
     *  displayed for in the console window
     */
    std::size_t duration;
};

#endif //WARZONE2100_CONSOLE_H
