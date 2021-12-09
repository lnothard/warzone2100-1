//
// Created by luna on 08/12/2021.
//

#ifndef WARZONE2100_STRUCTURE_H
#define WARZONE2100_STRUCTURE_H

#include "unit.h"
#include "droid.h"

enum class STRUCTURE_STATE
{
  BEING_BUILT,
  BUILT,
  BLUEPRINT_VALID,
  BLUEPRINT_INVALID,
  BLUEPRINT_PLANNED,
  BLUEPRINT_PLANNED_BY_ALLY,
};

class Structure : public virtual Unit
{
public:
  virtual ~Structure();
};

namespace Impl
{
  class Structure : public virtual ::Structure, public Impl::Unit
  {
  public:
    bool is_blueprint() const;
  private:
    using enum STRUCTURE_STATE;

    STRUCTURE_STATE state;
  };
}

struct Production_Job
{
  Droid_Template droid_template;
  uint32_t       time_started;
  int            points_remaining;
};

class Factory : public virtual Structure, public Impl::Structure
{
private:
  Production_Job active_job;
};

#endif // WARZONE2100_STRUCTURE_H