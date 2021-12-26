//
// Created by Luna Nothard on 26/12/2021.
//

#ifndef WARZONE2100_CHAT_H
#define WARZONE2100_CHAT_H

#include <string>
#include <set>
#include "lib/framework/frame.h"
#include "droid.h"

struct ChatMessage
{
    ChatMessage(unsigned sender, std::string message)
    : sender{sender}, message{message}
    {
    }

    [[nodiscard]] constexpr bool is_global() const
    {
      return !to_allies_only && recipients.empty();
    }

    [[nodiscard]] constexpr bool should_receive(unsigned player) const
    {
      return is_global() || recipients.find(player) != recipients.end() ||
             (to_allies_only && sender < MAX_PLAYERS && player < MAX_PLAYERS &&
                     alliance_formed(sender, player));
    }

    [[nodiscard]] constexpr std::vector<unsigned> get_recipients() const
    {

    }

    std::set<unsigned> recipients;
    unsigned sender;
    std::string message;
    bool to_allies_only;
};

#endif //WARZONE2100_CHAT_H
