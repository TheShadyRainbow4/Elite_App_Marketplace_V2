#pragma once
#include <string>
#include <vector>
#include <map>
#include <fstream>
#include <cstdint>
#include <cstring>
#include <iostream>
#include "json.hpp"
#include "miniz.h"

using json = nlohmann::json;

class ApkParser {
public:
    std::string packageName;
    std::string versionName;
    std::string label;
    std::string description;
    std::string iconPath;

    bool Parse(const std::string& apkPath) {
        mz_zip_archive zip_archive;
        memset(&zip_archive, 0, sizeof(zip_archive));
        if (!mz_zip_reader_init_file(&zip_archive, apkPath.c_str(), 0)) {
            return false;
        }

        std::vector<uint8_t> axmlData = ReadZipFile(&zip_archive, "AndroidManifest.xml");
        std::vector<uint8_t> arscData = ReadZipFile(&zip_archive, "resources.arsc");

        if (axmlData.empty()) {
            mz_zip_reader_end(&zip_archive);
            return false;
        }

        uint32_t iconId = 0;
        uint32_t labelId = 0;
        uint32_t descId = 0;

        ParseAxml(axmlData, iconId, labelId, descId);
        
        if (!arscData.empty()) {
            ParseArsc(arscData, iconId, labelId, descId);
        }

        if (iconPath.empty() || iconPath.find(".xml") != std::string::npos) {
            int num_files = mz_zip_reader_get_num_files(&zip_archive);
            int best_idx = -1;
            int best_score = -1;
            for (int i = 0; i < num_files; i++) {
                mz_zip_archive_file_stat file_stat;
                if (!mz_zip_reader_file_stat(&zip_archive, i, &file_stat)) continue;
                if (mz_zip_reader_is_file_a_directory(&zip_archive, i)) continue;
                std::string fname = file_stat.m_filename;
                std::string fnameLower = fname;
                std::transform(fnameLower.begin(), fnameLower.end(), fnameLower.begin(), ::tolower);
                if (fnameLower.find(".png") == std::string::npos && fnameLower.find(".webp") == std::string::npos) continue;

                int score = 0;
                if (fnameLower.find("res/") != std::string::npos) score += 10;
                if (fnameLower.find("mipmap") != std::string::npos) score += 10;
                if (fnameLower.find("drawable") != std::string::npos) score += 5;
                if (fnameLower.find("launcher") != std::string::npos) score += 1000;
                if (fnameLower.find("icon") != std::string::npos) score += 500;

                if (fnameLower.find("xxxhdpi") != std::string::npos) score += 50;
                else if (fnameLower.find("xxhdpi") != std::string::npos) score += 40;
                else if (fnameLower.find("xhdpi") != std::string::npos) score += 30;
                else if (fnameLower.find("hdpi") != std::string::npos) score += 20;
                else if (fnameLower.find("mdpi") != std::string::npos) score += 10;
                
                if (fnameLower.find("round") != std::string::npos) score += 5;

                if (score > best_score) {
                    best_score = score;
                    best_idx = i;
                    iconPath = fname;
                }
            }
        }

        mz_zip_reader_end(&zip_archive);
        return true;
    }

private:
    std::vector<uint8_t> ReadZipFile(mz_zip_archive* zip, const char* filename) {
        std::vector<uint8_t> data;
        int target_index = mz_zip_reader_locate_file(zip, filename, NULL, 0);
        if (target_index >= 0) {
            mz_zip_archive_file_stat stat;
            if (mz_zip_reader_file_stat(zip, target_index, &stat)) {
                data.resize((size_t)stat.m_uncomp_size);
                mz_zip_reader_extract_to_mem(zip, target_index, data.data(), data.size(), 0);
            }
        }
        return data;
    }

    void ParseAxml(const std::vector<uint8_t>& data, uint32_t& iconId, uint32_t& labelId, uint32_t& descId) {
        if (data.size() < 8) return;
        
        uint32_t offset = 8; // skip header
        std::vector<std::string> stringPool;

        // Parse String Pool
        uint32_t chunkType = *(uint32_t*)&data[offset];
        uint32_t chunkSize = *(uint32_t*)&data[offset + 4];
        if (chunkType == 0x001C0001) {
            uint32_t stringCount = *(uint32_t*)&data[offset + 8];
            uint32_t flags = *(uint32_t*)&data[offset + 16];
            uint32_t stringsOffset = *(uint32_t*)&data[offset + 20];
            bool isUTF8 = (flags & 0x100) != 0;

            uint32_t* stringOffsets = (uint32_t*)&data[offset + 28];
            for (uint32_t i = 0; i < stringCount; i++) {
                uint32_t strOff = offset + stringsOffset + stringOffsets[i];
                if (strOff >= data.size()) {
                    stringPool.push_back("");
                    continue;
                }
                if (isUTF8) {
                    uint32_t strDataOff = strOff;
                    if (strDataOff < data.size() && (data[strDataOff] & 0x80)) strDataOff += 2; else strDataOff += 1;
                    if (strDataOff < data.size() && (data[strDataOff] & 0x80)) strDataOff += 2; else strDataOff += 1;
                    std::string s;
                    while (strDataOff < data.size() && data[strDataOff] != 0) {
                        s += (char)data[strDataOff++];
                    }
                    stringPool.push_back(s);
                } else {
                    uint32_t strDataOff = strOff;
                    if (strDataOff + 1 < data.size() && (data[strDataOff + 1] & 0x80)) strDataOff += 4; else strDataOff += 2;
                    std::string s;
                    while (strDataOff + 1 < data.size()) {
                        uint16_t c = data[strDataOff] | (data[strDataOff + 1] << 8);
                        if (c == 0) break;
                        if (c < 128) s += (char)c; // hacky utf16 to ascii
                        strDataOff += 2;
                    }
                    stringPool.push_back(s);
                }
            }
            offset += chunkSize;
        }

        // Parse Tags
        while (offset + 8 <= data.size()) {
            uint32_t type = *(uint32_t*)&data[offset];
            uint32_t size = *(uint32_t*)&data[offset + 4];
            
            if (size < 8) break; // Prevent infinite loop on invalid size
            
            if (type == 0x00100102 && offset + 36 <= data.size()) { // START_TAG
                uint32_t nameIdx = *(uint32_t*)&data[offset + 20];
                std::string tagName = (nameIdx < stringPool.size()) ? stringPool[nameIdx] : "";
                
                uint32_t attrCount = *(uint16_t*)&data[offset + 28];
                uint32_t attrOffset = offset + 36;

                for (uint32_t i = 0; i < attrCount; i++) {
                    if (attrOffset + i * 20 + 20 > data.size()) break;
                    uint32_t attrNameIdx = *(uint32_t*)&data[attrOffset + i * 20 + 4];
                    uint32_t attrValStrIdx = *(uint32_t*)&data[attrOffset + i * 20 + 8];
                    uint32_t attrType = *(uint32_t*)&data[attrOffset + i * 20 + 12];
                    uint32_t attrData = *(uint32_t*)&data[attrOffset + i * 20 + 16];

                    std::string attrName = (attrNameIdx < stringPool.size()) ? stringPool[attrNameIdx] : "";

                    if (tagName == "manifest") {
                        if (attrName == "package" && attrValStrIdx < stringPool.size()) packageName = stringPool[attrValStrIdx];
                        if (attrName == "versionName" && attrValStrIdx < stringPool.size()) versionName = stringPool[attrValStrIdx];
                    } else if (tagName == "application") {
                        if (attrName == "icon") {
                            if (attrType == 0x01000008) iconId = attrData; // reference
                            else if (attrType == 0x03000008 && attrValStrIdx < stringPool.size()) iconPath = stringPool[attrValStrIdx]; // string
                        }
                        if (attrName == "label") {
                            if (attrType == 0x01000008) labelId = attrData;
                            else if (attrType == 0x03000008 && attrValStrIdx < stringPool.size()) label = stringPool[attrValStrIdx];
                        }
                        if (attrName == "description") {
                            if (attrType == 0x01000008) descId = attrData;
                            else if (attrType == 0x03000008 && attrValStrIdx < stringPool.size()) description = stringPool[attrValStrIdx];
                        }
                    }
                }
            }
            offset += size;
        }
    }

    void ParseArsc(const std::vector<uint8_t>& data, uint32_t iconId, uint32_t labelId, uint32_t descId) {
        if (data.size() < 12) return;
        
        uint32_t offset = 12; // skip header
        std::vector<std::string> stringPool;

        uint32_t chunkType = *(uint16_t*)&data[offset];
        uint32_t chunkSize = *(uint32_t*)&data[offset + 4];
        if (chunkType == 0x0001) { // RES_STRING_POOL_TYPE
            uint32_t stringCount = *(uint32_t*)&data[offset + 8];
            uint32_t flags = *(uint32_t*)&data[offset + 16];
            uint32_t stringsOffset = *(uint32_t*)&data[offset + 20];
            bool isUTF8 = (flags & 0x100) != 0;

            uint32_t* stringOffsets = (uint32_t*)&data[offset + 28];
            for (uint32_t i = 0; i < stringCount; i++) {
                uint32_t strOff = offset + stringsOffset + stringOffsets[i];
                if (strOff >= data.size()) {
                    stringPool.push_back("");
                    continue;
                }
                if (isUTF8) {
                    uint32_t strDataOff = strOff;
                    if (strDataOff < data.size() && (data[strDataOff] & 0x80)) strDataOff += 2; else strDataOff += 1;
                    if (strDataOff < data.size() && (data[strDataOff] & 0x80)) strDataOff += 2; else strDataOff += 1;
                    std::string s;
                    while (strDataOff < data.size() && data[strDataOff] != 0) {
                        s += (char)data[strDataOff++];
                    }
                    stringPool.push_back(s);
                } else {
                    uint32_t strDataOff = strOff;
                    if (strDataOff + 1 < data.size() && (data[strDataOff + 1] & 0x80)) strDataOff += 4; else strDataOff += 2;
                    std::string s;
                    while (strDataOff + 1 < data.size()) {
                        uint16_t c = data[strDataOff] | (data[strDataOff + 1] << 8);
                        if (c == 0) break;
                        if (c < 128) s += (char)c;
                        strDataOff += 2;
                    }
                    stringPool.push_back(s);
                }
            }
            offset += chunkSize;
        }

        // To reliably get resource values, we scan the whole ARSC table for ResTable_entry
        // A much simpler hack for icons is to find the string containing "res/" and ".png" that corresponds to our iconId.
        // Actually, we can just parse the packages and types.
        
        while (offset + 8 <= data.size()) {
            uint16_t type = *(uint16_t*)&data[offset];
            uint32_t size = *(uint32_t*)&data[offset + 4];
            
            if (size < 8) break; // Prevent infinite loop on invalid size
            
            if (type == 0x0200 && offset + 288 <= data.size()) { // RES_TABLE_PACKAGE_TYPE
                uint32_t typeStringsOff = *(uint32_t*)&data[offset + 268];
                uint32_t keyStringsOff = *(uint32_t*)&data[offset + 276];
                
                uint32_t innerOffset = offset + 288;
                while (innerOffset + 8 <= offset + size && innerOffset + 8 <= data.size()) {
                    uint16_t innerType = *(uint16_t*)&data[innerOffset];
                    uint32_t innerSize = *(uint32_t*)&data[innerOffset + 4];
                    
                    if (innerSize < 8) break; // Prevent infinite loop

                    if (innerType == 0x0201 && innerOffset + 20 <= data.size()) { // RES_TABLE_TYPE_TYPE
                        uint8_t id = data[innerOffset + 6]; // type ID
                        uint32_t entryCount = *(uint32_t*)&data[innerOffset + 12];
                        uint32_t entriesStart = *(uint32_t*)&data[innerOffset + 16];
                        
                        uint32_t* entryOffsets = (uint32_t*)&data[innerOffset + 20];
                        for (uint32_t i = 0; i < entryCount; i++) {
                            if (innerOffset + 20 + i * 4 + 4 > data.size()) break;
                            if (entryOffsets[i] != 0xFFFFFFFF) {
                                uint32_t entryOff = innerOffset + entriesStart + entryOffsets[i];
                                if (entryOff + 16 > data.size()) continue;
                                uint16_t entrySize = *(uint16_t*)&data[entryOff];
                                uint16_t flags = *(uint16_t*)&data[entryOff + 2];
                                
                                uint32_t resId = 0x7F000000 | (id << 16) | i;
                                
                                if ((flags & 0x0001) == 0) { // not complex
                                    uint32_t valStrIdx = *(uint32_t*)&data[entryOff + 8 + 4]; // Res_value data
                                    if (valStrIdx < stringPool.size()) {
                                        if (resId == iconId) {
                                            std::string path = stringPool[valStrIdx];
                                            if (path.find(".png") != std::string::npos || path.find(".webp") != std::string::npos) {
                                                iconPath = path;
                                            }
                                        }
                                        if (resId == labelId) label = stringPool[valStrIdx];
                                        if (resId == descId) description = stringPool[valStrIdx];
                                    }
                                }
                            }
                        }
                    }
                    innerOffset += innerSize;
                }
            }
            offset += size;
        }
    }
};
