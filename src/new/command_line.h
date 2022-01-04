//
// Created by Luna Nothard on 26/12/2021.
//

#ifndef WARZONE2100_COMMAND_LINE_H
#define WARZONE2100_COMMAND_LINE_H

#include <string>
#include <vector>

/// The type of command being executed
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
    /// The actual command text
    std::string text;

    /// A textual description of the command
    std::string description;
};

struct CommandContext
{
    std::vector<Command> commands;
};

///
void print_help_info();

/// Processes command text
void parse_command();

#endif //WARZONE2100_COMMAND_LINE_H
