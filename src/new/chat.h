//
// Created by Luna Nothard on 26/12/2021.
//

#ifndef WARZONE2100_CHAT_H
#define WARZONE2100_CHAT_H

#include <string>
#include <set>
#include "lib/framework/frame.h"
#include "droid.h"

/**
 *
 */
struct ChatMessage
{
    ChatMessage(unsigned sender, std::string message);

    /// @return `true` if seen by all players
    [[nodiscard]] bool is_global() const;

    /**
     * @param player The ID of the player in question
     * @return `true` if `player` is a valid recipient
     */
    [[nodiscard]] bool should_receive(unsigned player) const;

     /// @return A list of the actual recipients of this message
    [[nodiscard]] std::vector<unsigned> get_recipients() const;

    /**
     * If empty, the message is sent to all players if
     * `is_global()` returns true. Otherwise, send the message
     * only to the players contained within the `intended_recipients` set.
     */
    std::set<unsigned> intended_recipients;

    /// Uniquely identifies the player sending this message
    unsigned sender_id = 0;

    /// The actual text to be displayed
    std::string message_text {};

    /**
     * `true` if the message is private, i.e., shown only
     * to allies of `sender_id`
     */
    bool allies_only = false;
};

#endif //WARZONE2100_CHAT_H
