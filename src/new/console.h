//
// Created by Luna Nothard on 27/12/2021.
//

#ifndef WARZONE2100_CONSOLE_H
#define WARZONE2100_CONSOLE_H

#include <string>

enum class TEXT_JUSTIFICATION
{
    LEFT,
    RIGHT,
    CENTRE,
    DEFAULT = LEFT
};

struct ConsoleMessage
{
    std::string text;
    TEXT_JUSTIFICATION justification;
    unsigned sender;
    std::size_t duration;
};

#endif //WARZONE2100_CONSOLE_H
