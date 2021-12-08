//
// Created by luna on 08/12/2021.
//

#ifndef WARZONE2100_DROID_GROUP_H
#define WARZONE2100_DROID_GROUP_H

#include "droid.h"

enum class GROUP_TYPE
{
  NORMAL,
  COMMAND,
  TRANSPORTER
};

class Droid_Group
{
public:
  void add(Droid *droid);
  void remove(Droid *droid);
  bool is_command_group() const;
private:
  using enum GROUP_TYPE;

  uint32_t               id;
  GROUP_TYPE             type;
  std::unique_ptr<Droid> commander;
  std::vector<Droid>     members;
};


#endif // WARZONE2100_DROID_GROUP_H