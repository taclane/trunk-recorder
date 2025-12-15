#ifndef UNIT_TAG_H
#define UNIT_TAG_H

#include <iostream>
#include <stdio.h>
#include <string>
#include <boost/regex.hpp>

// User defined tag structure with regex pattern matching
class UnitTag {
public:
  boost::regex pattern;
  std::string tag;

  UnitTag(std::string p, std::string t);
};

// Simplified OTA tag structure for fast number-to-alias lookups
class UnitTagOTA {
public:
  long unit_id;
  std::string alias;

  UnitTagOTA(long id, std::string a);
};

#endif // UNIT_TAG_H
