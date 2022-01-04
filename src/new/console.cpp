//
// Created by Luna Nothard on 04/01/2022.
//

#include "console.h"

ConsoleMessage::ConsoleMessage(std::string text, unsigned sender)
  : text{std::move(text)}, sender{sender}
{
}
