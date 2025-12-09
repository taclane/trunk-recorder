#ifndef UNIT_TAGS_OTA_H
#define UNIT_TAGS_OTA_H

#include <string>
#include <vector>
#include <array>
#include <cstdint>

// Result structure for OTA alias decode containing radio ID, alias text, and source
struct OTAAlias {
  bool success;           // Whether decode was successful
  unsigned long radio_id; // Radio ID from the alias payload
  std::string alias;      // Decoded alias text
  std::string source;     // Decoder source (e.g. "MotoP25_TDMA", "MotoP25_FDMA")
  
  OTAAlias() : success(false), radio_id(0), alias(""), source("") {}
  OTAAlias(unsigned long id, const std::string& text, const std::string& src = "") 
    : success(true), radio_id(id), alias(text), source(src) {}
};

class UnitTagsOTA {
public:
  // Motorola OTA (Over-The-Air) alias decoding
  static OTAAlias decode_motorola_alias(const std::array<std::vector<uint8_t>, 10>& alias_buffer, int messages);
  static OTAAlias decode_motorola_alias_p2(const std::array<std::vector<uint8_t>, 10>& alias_buffer, int messages);

private:
  // Helper functions for Motorola alias decoding
  static std::string assemble_payload(const std::array<std::vector<uint8_t>, 10>& alias_buffer, int messages);
  static std::string assemble_payload_p2(const std::array<std::vector<uint8_t>, 10>& alias_buffer, int messages);
  static bool validate_crc(const std::string& payload_hex, const std::string& checksum_hex);
  static std::string decode_mot_alias(const std::vector<int8_t>& encoded_data);
};

#endif // UNIT_TAGS_OTA_H
