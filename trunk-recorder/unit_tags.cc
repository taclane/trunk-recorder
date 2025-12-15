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

void UnitTags::load_unit_tags(std::string filename) {
  if (filename == "") {
    return;
  }

  CSVFormat format;
  format.trim({' ', '\t'});
  format.header_row(-1);  // No header row expected
  format.column_names({"unit_id", "tag"});
  
  try {
    CSVReader reader(filename, format);
    
    int lines_loaded = 0;
    for (CSVRow &row : reader) {
      if (row.size() < 2) {
        continue;
      }
      
      // First column: unit ID pattern (decimal or regex)
      // Second column: tag/alias
      std::string pattern = row["unit_id"].get<>();
      std::string tag = row["tag"].get<>();
      
      add(pattern, tag);
      lines_loaded++;
    }
    
    BOOST_LOG_TRIVIAL(info) << "Read " << lines_loaded << " unit tags.";
  } catch (std::exception &e) {
    BOOST_LOG_TRIVIAL(error) << "Error reading Unit Tag File: " << filename << " - " << e.what();
  }
}

void UnitTags::load_unit_tags_ota(std::string filename) {
  ota_filename = filename;
  
  if (filename == "") {
    return;
  }

  if (mode == TAG_NONE) {
    return;
  }

  // Check if file exists
  std::ifstream test(filename);
  if (!test.good()) {
    return;  // File doesn't exist yet, that's ok!
  }
  test.close();

  CSVFormat format;
  format.trim({' ', '\t'});
  format.header_row(-1);  // No header row
  format.column_names({"unit_id", "tag", "source", "timestamp"});
  
  try {
    CSVReader reader(filename, format);
    
    int lines_loaded = 0;
    for (CSVRow &row : reader) {
      if (row.size() < 2) {
        continue;
      }

      // First column: unit ID pattern (decimal)
      // Second column: tag/alias
      long unit_id = std::stol(row["unit_id"].get<>());
      std::string tag = row["tag"].get<>();
      
      UnitTagOTA *ota_tag = new UnitTagOTA(unit_id, tag);
      unit_tags_ota.push_back(ota_tag);
      lines_loaded++;
    }
    
    if (lines_loaded > 0) {
      BOOST_LOG_TRIVIAL(info) << "Loaded " << lines_loaded << " OTA unit tags.";
    }
  } catch (std::exception &e) {
    BOOST_LOG_TRIVIAL(error) << "Error reading OTA Unit Tag File: " << filename << " - " << e.what();
  }
}

std::string UnitTags::find_unit_tag(long tg_number) {
  // TAG_NONE: Don't search any tags
  if (mode == TAG_NONE) {
    return "";
  }

  std::string tg_num_str = std::to_string(tg_number);
  
  // Helper lambda: Search user tags
  auto search_user_tags = [&]() -> std::string {
    for (std::vector<UnitTag *>::iterator it = unit_tags.begin(); it != unit_tags.end(); ++it) {
      UnitTag *tg = (UnitTag *)*it;
      if (regex_match(tg_num_str, tg->pattern)) {
        return regex_replace(tg_num_str, tg->pattern, tg->tag, boost::regex_constants::format_no_copy | boost::regex_constants::format_all);
      }
    }
    return "";
  };
  
  // Helper lambda: Search OTA tags
  auto search_ota_tags = [&]() -> std::string {
    for (auto it = unit_tags_ota.rbegin(); it != unit_tags_ota.rend(); ++it) {
      UnitTagOTA *ota_tag = *it;
      if (ota_tag->unit_id == tg_number) {
        return ota_tag->alias;
      }
    }
    return "";
  };
  
  // TAG_USER_FIRST: Search user tags first, then OTA
  if (mode == TAG_USER_FIRST) {
    std::string tag = search_user_tags();
    if (!tag.empty()) return tag;
    return search_ota_tags();
  }
  
  // TAG_OTA_FIRST: Search OTA tags first, then user tags
  if (mode == TAG_OTA_FIRST) {
    std::string tag = search_ota_tags();
    if (!tag.empty()) return tag;
    return search_user_tags();
  }

  // TAG_USER_ONLY: Only search user tags
  if (mode == TAG_USER_ONLY) {
    return search_user_tags();
  }

  return "";
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

bool UnitTags::add_ota(long unitID, std::string tag, std::string source) {
  // If mode is TAG_NONE, don't process or write OTA tags
  if (mode == TAG_NONE) {
    return false;
  }
  
  // Check if this unit already has an OTA tag (search OTA list only)
  std::string existing_ota_tag = "";
  for (auto it = unit_tags_ota.rbegin(); it != unit_tags_ota.rend(); ++it) {
    UnitTagOTA *ota_tag = *it;
    if (ota_tag->unit_id == unitID) {
      existing_ota_tag = ota_tag->alias;
      break;
    }
  }
  
  if (!existing_ota_tag.empty()) {
    if (existing_ota_tag == tag) {
      BOOST_LOG_TRIVIAL(debug) << "Unit " << unitID << " has existing OTA alias: '" << tag << "', skipping";
      return false;
    }
    BOOST_LOG_TRIVIAL(debug) << "Unit " << unitID << " OTA alias updated: '" << existing_ota_tag << "' -> '" << tag << "'";
  }
  
  UnitTagOTA *ota_tag = new UnitTagOTA(unitID, tag);
  unit_tags_ota.push_back(ota_tag);

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

void UnitTags::set_mode(UnitTagMode mode) {
  this->mode = mode;
}

UnitTagMode UnitTags::get_mode() {
  return this->mode;
}
