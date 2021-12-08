//
// Created by luna on 08/12/2021.
//

#include "structure.h"

namespace Impl
{
  bool Structure::is_blueprint() const
  {
    return (state == BLUEPRINT_VALID ||
            state == BLUEPRINT_INVALID ||
            state == BLUEPRINT_PLANNED ||
            state == BLUEPRINT_PLANNED_BY_ALLY);
  }
}