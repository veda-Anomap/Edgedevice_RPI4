#include "ConfigLoader.h"
#include <fstream>
#include <iostream>

bool ConfigLoader::load(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) {
        std::cerr << "[ConfigLoader] 파일 열기 실패: " << path << "\n";
        return false;
    }
    try {
        f >> json_;
    } catch (const std::exception& e) {
        std::cerr << "[ConfigLoader] JSON 파싱 오류: " << e.what() << "\n";
        return false;
    }

    for (auto it = json_.begin(); it != json_.end(); ++it) {
        if (it.key().front() != '_' && it->is_object()) {
            parseSection(it.key());
        }
    }
    std::cout << "[ConfigLoader] 로드 완료: " << path << "\n";
    return true;
}

void ConfigLoader::parseSection(const std::string& section) {
    if (!json_.contains(section)) return;
    auto& sec = json_[section];
    for (auto it = sec.begin(); it != sec.end(); ++it) {
        ParamEntry e;
        auto& obj = it.value();
        if (obj.contains("value")) {
            if (obj["value"].is_string()) {
                e.strValue = obj["value"].get<std::string>();
            } else {
                e.value = obj["value"].get<double>();
            }
        }
        if (obj.contains("default")) {
            if (obj["default"].is_string()) {
                e.strValue = obj["default"].get<std::string>();
            } else {
                e.def = obj["default"].get<double>();
            }
        }
        if (obj.contains("min"))  e.minVal = obj["min"].get<double>();
        if (obj.contains("max"))  e.maxVal = obj["max"].get<double>();
        if (obj.contains("desc")) e.desc   = obj["desc"].get<std::string>();
        params_[section][it.key()] = e;
    }
}

bool ConfigLoader::save(const std::string& path) const {
    nlohmann::json out = json_;
    // value 필드만 현재 파라미터로 업데이트
    for (auto& [section, keys] : params_) {
        for (auto& [key, entry] : keys) {
            if (out.contains(section) && out[section].contains(key)) {
                if (!entry.strValue.empty()) {
                    out[section][key]["value"] = entry.strValue;
                } else {
                    out[section][key]["value"] = entry.value;
                }
            }
        }
    }
    std::ofstream f(path);
    if (!f.is_open()) {
        std::cerr << "[ConfigLoader] 저장 실패: " << path << "\n";
        return false;
    }
    f << out.dump(2);
    std::cout << "[ConfigLoader] 저장 완료: " << path << "\n";
    return true;
}

double ConfigLoader::get(const std::string& section, const std::string& key) const {
    auto sit = params_.find(section);
    if (sit == params_.end()) return 0.0;
    auto kit = sit->second.find(key);
    if (kit == sit->second.end()) return 0.0;
    return kit->second.value;
}

void ConfigLoader::set(const std::string& section, const std::string& key, double val) {
    params_[section][key].value = val;
}

std::string ConfigLoader::getStr(const std::string& section, const std::string& key) const {
    auto sit = params_.find(section);
    if (sit == params_.end()) return "";
    auto kit = sit->second.find(key);
    if (kit == sit->second.end()) return "";
    return kit->second.strValue;
}

void ConfigLoader::setStr(const std::string& section, const std::string& key, const std::string& val) {
    params_[section][key].strValue = val;
}

const std::unordered_map<std::string, ParamEntry>&
ConfigLoader::getSection(const std::string& section) const {
    static std::unordered_map<std::string, ParamEntry> empty;
    auto it = params_.find(section);
    return (it != params_.end()) ? it->second : empty;
}

double ConfigLoader::getMin(const std::string& s, const std::string& k) const {
    auto sit = params_.find(s);
    if (sit == params_.end()) return 0.0;
    auto kit = sit->second.find(k);
    return (kit != sit->second.end()) ? kit->second.minVal : 0.0;
}

double ConfigLoader::getMax(const std::string& s, const std::string& k) const {
    auto sit = params_.find(s);
    if (sit == params_.end()) return 1.0;
    auto kit = sit->second.find(k);
    return (kit != sit->second.end()) ? kit->second.maxVal : 1.0;
}

double ConfigLoader::getDefault(const std::string& s, const std::string& k) const {
    auto sit = params_.find(s);
    if (sit == params_.end()) return 0.0;
    auto kit = sit->second.find(k);
    return (kit != sit->second.end()) ? kit->second.def : 0.0;
}
