//
// Created by Luna Nothard on 02/01/2022.
//

#include "chat.h"

ChatMessage::ChatMessage(unsigned sender, std::string message)
        : sender_id{sender}, message_text{std::move(message)}
{
}

[[nodiscard]] bool ChatMessage::is_global() const
{
  return !allies_only && intended_recipients.empty();
}

[[nodiscard]] bool ChatMessage::should_receive(unsigned player) const
{
  // if `is_global() returns `true`, the `player` does not matter,
  // since all receive the message
  return is_global() ||
  // if `player` is not found in `intended_recipients`, return `false`
  // if `allies_only` is set to `true`, and there is an alliance between `player`
  // and `sender_id`, return `true`
  intended_recipients.find(player) != intended_recipients.end() ||
         (allies_only && sender_id < MAX_PLAYERS && player < MAX_PLAYERS &&
            alliance_formed(sender_id, player));
}

[[nodiscard]] std::vector<unsigned> ChatMessage::get_recipients() const
{

}
