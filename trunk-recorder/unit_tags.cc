#include "unit_tags.h"

#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/intrusive_ptr.hpp>
#include <boost/log/trivial.hpp>
#include <boost/tokenizer.hpp>
#include <boost/regex.hpp>

// #include "csv_helper.h"
#include <csv-parser/csv.hpp>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>

using namespace csv;

UnitTags::UnitTags() {}

void UnitTags::load_unit_tags(std::string filename) {
  if (filename == "") {
    return;
  }

  CSVFormat format;
  format.trim({' ', '\t'});
  format.header_row(-1);  // No header row expected
  
  try {
    CSVReader reader(filename, format);
    
    int lines_loaded = 0;
    for (CSVRow &row : reader) {
      if (row.size() < 2) {
        continue;
      }
      
      // First column: unit ID pattern (decimal or regex)
      // Second column: tag/alias
      std::string pattern = row[0].get<>();
      std::string tag = row[1].get<>();
      
      add(pattern, tag);
      lines_loaded++;
    }
    
    BOOST_LOG_TRIVIAL(info) << "Read " << lines_loaded << " unit tags.";
  } catch (std::exception &e) {
    BOOST_LOG_TRIVIAL(error) << "Error reading Unit Tag File: " << filename << " - " << e.what();
  }
}

std::string UnitTags::find_unit_tag(long tg_number) {
  std::string tg_num_str = std::to_string(tg_number);
  std::string tag = "";

  for (std::vector<UnitTag *>::iterator it = unit_tags.begin(); it != unit_tags.end(); ++it) {
    UnitTag *tg = (UnitTag *)*it;

    if (regex_match(tg_num_str, tg->pattern)) {
      tag = regex_replace(tg_num_str, tg->pattern, tg->tag, boost::regex_constants::format_no_copy | boost::regex_constants::format_all);
      break;
    }
  }

  return tag;
}

void UnitTags::add(std::string pattern, std::string tag) {
  // If the pattern is like /someregex/
  if (pattern.substr(0, 1).compare("/") == 0 && pattern.substr(pattern.length()-1, 1).compare("/") == 0) {
    // then remove the / at the beginning and end
    pattern = pattern.substr(1, pattern.length()-2);
  } else {
    // otherwise add ^ and $ to the pattern e.g. ^123$ to make a regex for simple IDs
    pattern = "^" + pattern + "$";
  }
  UnitTag *unit_tag = new UnitTag(pattern, tag);
  unit_tags.push_back(unit_tag);
}

void UnitTags::load_unit_tags_ota(std::string filename) {
  ota_filename = filename;
  
  if (filename == "") {
    return;
  }

  // Check if file exists
  std::ifstream test(filename);
  if (!test.good()) {
    return;  // File doesn't exist yet, that's ok
  }
  test.close();
  
  CSVFormat format;
  format.trim({' ', '\t'});
  format.header_row(-1);  // No header row
  
  try {
    CSVReader reader(filename, format);
    
    int lines_loaded = 0;
    for (CSVRow &row : reader) {
      if (row.size() < 2) {
        continue;
      }
      
      // Format: unitID,tag[,source,timestamp]
      std::string pattern = "^" + row[0].get<>() + "$";
      std::string tag = row[1].get<>();
      
      UnitTag *unit_tag = new UnitTag(pattern, tag);
      unit_tags.insert(unit_tags.begin(), unit_tag);
      lines_loaded++;
    }
    
    if (lines_loaded > 0) {
      BOOST_LOG_TRIVIAL(info) << "Loaded " << lines_loaded << " OTA unit tags.";
    }
  } catch (std::exception &e) {
    BOOST_LOG_TRIVIAL(error) << "Error reading OTA Unit Tag File: " << filename << " - " << e.what();
  }
}

bool UnitTags::addFront(long unitID, std::string tag, std::string source) {
  std::string pattern = "^" + std::to_string(unitID) + "$";
  
  // Check if this unit already has a tag
  std::string existing_tag = find_unit_tag(unitID);
  if (!existing_tag.empty()) {
    if (existing_tag == tag) {
      BOOST_LOG_TRIVIAL(debug) << "Unit " << unitID << " has existing alias: '" << tag << "', skipping";
      return false;
    }
    BOOST_LOG_TRIVIAL(debug) << "Unit " << unitID << " alias updated: '" << existing_tag << "' -> '" << tag << "'";
  }
  
  // Add to front of list (takes precedence over older entries)
  UnitTag *unit_tag = new UnitTag(pattern, tag);
  unit_tags.insert(unit_tags.begin(), unit_tag);

  // Write to OTA file if configured
  if (!ota_filename.empty()) {
    try {
      std::ofstream out(ota_filename, std::ios::app);
      if (out.is_open()) {
        CSVWriter<std::ofstream> writer(out);
        writer << std::vector<std::string>{std::to_string(unitID), tag, source, std::to_string(std::time(nullptr))};
        out.close();
      } else {
        BOOST_LOG_TRIVIAL(error) << "Failed to open " << ota_filename << " for writing OTA alias.";
      }
    } catch (std::exception &e) {
      BOOST_LOG_TRIVIAL(error) << "Error writing to OTA file " << ota_filename << ": " << e.what();
    }
  }
  
  return true;
}
