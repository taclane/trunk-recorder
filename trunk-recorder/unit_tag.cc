#include "unit_tag.h"
#include <boost/regex.hpp>

UnitTag::UnitTag(std::string p, std::string t) {
  pattern = p;
  tag = t;
}

UnitTagOTA::UnitTagOTA(long id, std::string a) {
  unit_id = id;
  alias = a;
}
