/**
 *  Copyright (c) 2014-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under both the Apache 2.0 license (found in the
 *  LICENSE file in the root directory of this source tree) and the GPLv2 (found
 *  in the COPYING file in the root directory of this source tree).
 *  You may select, at your option, one of the above-listed licenses.
 */

#include <boost/algorithm/string/trim.hpp>

#include "osquery/tables/system/smbios_utils.h"
#include "osquery/core/hashing.h"

namespace osquery {
namespace tables {

const std::map<uint8_t, std::string> kSMBIOSTypeDescriptions = {
    {0, "BIOS Information"},
    {1, "System Information"},
    {2, "Base Board or Module Information"},
    {3, "System Enclosure or Chassis"},
    {4, "Processor Information"},
    {5, "Memory Controller Information"},
    {6, "Memory Module Information"},
    {7, "Cache Information"},
    {8, "Port Connector Information"},
    {9, "System Slots"},
    {10, "On Board Devices Information"},
    {11, "OEM Strings"},
    {12, "System Configuration Options"},
    {13, "BIOS Language Information"},
    {14, "Group Associations"},
    {15, "System Event Log"},
    {16, "Physical Memory Array"},
    {17, "Memory Device"},
    {18, "32-bit Memory Error Information"},
    {19, "Memory Array Mapped Address"},
    {20, "Memory Device Mapped Address"},
    {21, "Built-in Pointing Device"},
    {22, "Portable Battery"},
    {23, "System Reset"},
    {24, "Hardware Security"},
    {25, "System Power Controls"},
    {26, "Voltage Probe"},
    {27, "Cooling Device"},
    {28, "Temperature Probe"},
    {29, "Electrical Current Probe"},
    {30, "Out-of-Band Remote Access"},
    {31, "Boot Integrity Services"},
    {32, "System Boot Information"},
    {33, "64-bit Memory Error Information"},
    {34, "Management Device"},
    {35, "Management Device Component"},
    {36, "Management Device Threshold Data"},
    {37, "Memory Channel"},
    {38, "IPMI Device Information"},
    {39, "System Power Supply"},
    {40, "Additional Information"},
    {41, "Onboard Devices Extended Info"},
    {126, "Inactive"},
    {127, "End-of-Table"},
    {130, "Memory SPD Data"},
    {131, "OEM Processor Type"},
    {132, "OEM Processor Bus Speed"},
};

const std::map<uint8_t, std::string> kSMBIOSMemoryFormFactorTable = {
    {0x01, "Other"},
    {0x02, "Unknown"},
    {0x03, "SIMM"},
    {0x04, "SIP"},
    {0x05, "Chip"},
    {0x06, "DIP"},
    {0x07, "ZIP"},
    {0x08, "Proprietary Card"},
    {0x09, "DIMM"},
    {0x0A, "TSOP"},
    {0x0B, "Row of chips"},
    {0x0C, "RIMM"},
    {0x0D, "SODIMM"},
    {0x0E, "SRIMM"},
    {0x0F, "FB-DIMM"},
};

const std::map<uint8_t, std::string> kSMBIOSMemoryDetailsTable = {
    {0, "Reserved"},
    {1, "Other"},
    {2, "Unknown"},
    {3, "Fast-paged"},
    {4, "Static column"},
    {5, "Pseudo-static"},
    {6, "RAMBUS"},
    {7, "Synchronous"},
    {8, "CMOS"},
    {9, "EDO"},
    {10, "Window DRAM"},
    {11, "Cache DRAM"},
    {12, "Non-volatile"},
    {13, "Registered (Buffered)"},
    {14, "Unbuffered (Unregistered)"},
    {15, "LRDIMM"},
};

const std::map<uint8_t, std::string> kSMBIOSMemoryTypeTable = {
    {0x01, "Other"},    {0x02, "Unknown"},      {0x03, "DRAM"},
    {0x04, "EDRAM"},    {0x05, "VRAM"},         {0x06, "SRAM"},
    {0x07, "RAM"},      {0x08, "ROM"},          {0x09, "FLASH"},
    {0x0A, "EEPROM"},   {0x0B, "FEPROM"},       {0x0C, "EPROM"},
    {0x0D, "CDRAM"},    {0x0E, "3DRAM"},        {0x0F, "SDRAM"},
    {0x10, "SGRAM"},    {0x11, "RDRAM"},        {0x12, "DDR"},
    {0x13, "DDR2"},     {0x14, "DDR2 FB-DIMM"}, {0x15, "RESERVED"},
    {0x16, "RESERVED"}, {0x17, "RESERVED"},     {0x18, "DDR3"},
    {0x19, "FBD2"},     {0x1A, "DDR4"},         {0x1B, "LPDDR"},
    {0x1C, "LPDDR2"},   {0x1D, "LPDDR3"},       {0x1E, "LPDDR4"},
};

template <class T>
static inline std::string toHexStr(T num, int width = 4) {
  std::stringstream ss;
  ss << std::hex << std::setw(width) << std::setfill('0') << num;
  return "0x" + ss.str();
}

/**
 * SMBIOS data in the formatted section can BYTE, WORD, DWORD, QWORD lengths.
 * They begin at an offset of the structure examined until the end of
 * length specificed in
 * https://www.dmtf.org/sites/default/files/standards/documents/DSP0134_3.1.1.pdf
 **/

/**
 * @brief Returns uint16_t representation of a WORD length field
 *
 *
 * @param address A pointer to the examined structure.
 * @Param offset The field index into address.
 */
inline uint16_t dmiToWord(uint8_t* address, uint8_t offset) {
  return (static_cast<uint16_t>(address[offset + 1]) << 8) |
         static_cast<uint16_t>(address[offset]);
}

/**
 * @brief Returns uint32_t representation of a DWORD length field
 *
 *
 * @param address A pointer to the examined structure.
 * @Param offset The field index into address.
 */
inline uint32_t dmiToDWord(uint8_t* address, uint8_t offset) {
  return (static_cast<uint32_t>(address[offset + 3]) << 24) |
         (static_cast<uint32_t>(address[offset + 2]) << 16) |
         (static_cast<uint32_t>(address[offset + 1]) << 8) |
         static_cast<uint32_t>(address[offset]);
}

static inline std::string dmiWordToHexStr(uint8_t* address, uint8_t offset) {
  auto word = dmiToWord(address, offset);
  return toHexStr(word);
}

void SMBIOSParser::tables(std::function<void(size_t index,
                                             const SMBStructHeader* hdr,
                                             uint8_t* address,
                                             size_t size)> predicate) {
  if (table_data_ == nullptr) {
    return;
  }

  // Keep a pointer to the end of the SMBIOS data for comparison.
  auto tables_end = table_data_ + table_size_;
  auto table = table_data_;

  // Iterate through table structures within SMBIOS data range.
  size_t index = 0;
  while (table + sizeof(SMBStructHeader) <= tables_end) {
    auto header = (const SMBStructHeader*)table;
    if (table + header->length > tables_end) {
      // Invalid header, length must be within SMBIOS data range.
      break;
    }

    if (header->length == 0 && header->handle == 0) {
      // Reached the end (null-padded content).
      break;
    }

    // The SMBIOS structure may have unformatted, double-NULL delimited
    // trailing data, which are usually strings.
    auto next_table = table + header->length;
    for (; next_table + sizeof(SMBStructHeader) <= tables_end; next_table++) {
      if (next_table[0] == 0 && next_table[1] == 0) {
        next_table += 2;
        break;
      }
    }

    auto table_length = next_table - table;
    predicate(index++, header, table, table_length);
    table = next_table;
  }
}

void genSMBIOSTable(size_t index,
                    const SMBStructHeader* hdr,
                    uint8_t* address,
                    size_t size,
                    QueryData& results) {
  Row r;
  // The index is a supliment that keeps track of table order.
  r["number"] = INTEGER(index++);
  r["type"] = INTEGER((unsigned short)hdr->type);
  if (kSMBIOSTypeDescriptions.count(hdr->type) > 0) {
    r["description"] = kSMBIOSTypeDescriptions.at(hdr->type);
  } else {
    r["description"] = "Unknown";
  }

  r["handle"] = BIGINT((unsigned long long)hdr->handle);
  r["header_size"] = INTEGER((unsigned short)hdr->length);

  r["size"] = INTEGER(size);
  r["md5"] = hashFromBuffer(HASH_TYPE_MD5, address, size);
  results.push_back(r);
}

void genSMBIOSMemoryDevices(size_t index,
                            const SMBStructHeader* hdr,
                            uint8_t* address,
                            size_t size,
                            QueryData& results) {
  if (hdr->type != kSMBIOSTypeMemoryDevice || size < 0x12) {
    return;
  }

  Row r;
  r["handle"] = dmiWordToHexStr(address, 0x02);
  r["array_handle"] = dmiWordToHexStr(address, 0x04);

  auto formFactor = kSMBIOSMemoryFormFactorTable.find(address[0x0E]);
  if (formFactor != kSMBIOSMemoryFormFactorTable.end()) {
    r["form_factor"] = formFactor->second;
  }

  auto memBits = dmiToWord(address, 0x08);
  if (memBits != 0xFFFF) {
    r["total_width"] = INTEGER(memBits);
  }

  memBits = dmiToWord(address, 0x0A);
  if (memBits != 0xFFFF) {
    r["data_width"] = INTEGER(memBits);
  }

  memBits = dmiToWord(address, 0x0C);
  if (memBits != 0xFFFF) {
    r["size"] = (memBits != 0x7FFF) ? INTEGER(memBits)
                                    : INTEGER(dmiToDWord(address, 0x1C));
  }

  if (address[0x0F] != 0xFF) {
    r["set"] = INTEGER(static_cast<int>(address[0x0F]));
  }

  uint8_t* data = address + hdr->length;
  r["device_locator"] = dmiString(data, address, 0x10);
  r["bank_locator"] = dmiString(data, address, 0x11);

  auto memoryType = kSMBIOSMemoryTypeTable.find(address[0x12]);
  if (memoryType != kSMBIOSMemoryTypeTable.end()) {
    r["memory_type"] = memoryType->second;
  }

  r["memory_type_details"] =
      dmiBitFieldToStr(dmiToWord(address, 0x13), kSMBIOSMemoryDetailsTable);

  auto speed = dmiToWord(address, 0x15);
  if (speed != 0x0000 && speed != 0xFFFF) {
    r["max_speed"] = INTEGER(speed);
  }

  speed = dmiToWord(address, 0x20);
  if (speed != 0x0000 && speed != 0xFFFF) {
    r["configured_clock_speed"] = INTEGER(speed);
  }

  r["manufacturer"] = dmiString(data, address, 0x17);
  r["serial_number"] = dmiString(data, address, 0x18);
  r["asset_tag"] = dmiString(data, address, 0x19);
  r["part_number"] = dmiString(data, address, 0x1A);

  auto vt = dmiToWord(address, 0x22);
  if (vt != 0) {
    r["min_voltage"] = INTEGER(vt);
  }

  vt = dmiToWord(address, 0x24);
  if (vt != 0) {
    r["max_voltage"] = INTEGER(vt);
  }

  vt = dmiToWord(address, 0x26);
  if (vt != 0) {
    r["configured_voltage"] = INTEGER(vt);
  }

  results.push_back(std::move(r));
}

std::string dmiString(uint8_t* data, uint8_t* address, size_t offset) {
  auto index = address[offset];
  if (index == 0) {
    return "";
  }

  auto bp = reinterpret_cast<char*>(data);
  while (index > 1) {
    while (*bp != 0) {
      bp++;
    }
    bp++;
    index--;
  }

  std::string str{bp};
  // Sometimes vendors leave extraneous spaces on the right side.
  boost::algorithm::trim_right(str);
  return str;
}

std::string dmiBitFieldToStr(size_t bitField,
                             const std::map<uint8_t, std::string>& table) {
  std::string result;

  for (uint8_t i = 0; i < table.size(); i++) {
    if (1 << i & bitField) {
      result = result + table.at(i) + ' ';
    }
  }

  if (!result.empty()) {
    result.pop_back();
  }

  return result;
}

} // namespace tables
} // namespace osquery
