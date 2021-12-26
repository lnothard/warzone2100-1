//
// Created by Luna Nothard on 26/12/2021.
//

#ifndef WARZONE2100_COMMAND_LINE_H
#define WARZONE2100_COMMAND_LINE_H

#include <string>
#include <vector>

enum class CLI_OPTIONS
{
    CONFIG_DIR,
    DATA_DIR,
    DEBUG,
    DEBUG_FILE,
    FLUSH_DEBUG,
    FULLSCREEN,
    GAME,
    HELP,

};

struct Command
{
    std::string text;
    std::string description;
};

struct CommandContext
{
    std::vector<Command> commands;
};

void print_help_info();
void parse_command();

#endif //WARZONE2100_COMMAND_LINE_H
