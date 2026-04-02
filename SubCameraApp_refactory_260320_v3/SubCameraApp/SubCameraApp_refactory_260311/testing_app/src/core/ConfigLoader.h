#pragma once
#include <string>
#include <unordered_map>
#include "nlohmann/json.hpp"

// =============================================
// ParamEntry: 파라미터 하나의 메타데이터
// =============================================
struct ParamEntry {
    double value   = 0.0;
    double def     = 0.0;
    double minVal  = 0.0;
    double maxVal  = 0.0;
    std::string desc;
    std::string strValue; // 문자열 파라미터용 (ai_mode, model_path 등)
};

// =============================================
// ConfigLoader: JSON 파라미터 로드/저장 (SRP)
// =============================================
class ConfigLoader {
public:
    // JSON 파일을 로드하여 파라미터 맵 초기화
    bool load(const std::string& path);

    // 현재 파라미터 값을 JSON으로 저장
    bool save(const std::string& path) const;

    // 파라미터 수치 접근
    double get(const std::string& section, const std::string& key) const;
    void   set(const std::string& section, const std::string& key, double val);

    // 파라미터 문자열 접근 (ai_mode, model_path 등)
    std::string getStr(const std::string& section, const std::string& key) const;
    void        setStr(const std::string& section, const std::string& key, const std::string& val);

    // 전체 섹션 접근
    const std::unordered_map<std::string, ParamEntry>& getSection(const std::string& section) const;

    // 유효 범위 반환
    double getMin(const std::string& section, const std::string& key) const;
    double getMax(const std::string& section, const std::string& key) const;
    double getDefault(const std::string& section, const std::string& key) const;

    // 로드된 JSON 전체 (raw 접근)
    const nlohmann::json& rawJson() const { return json_; }

private:
    nlohmann::json json_;
    // 섹션 → (키 → ParamEntry)
    std::unordered_map<std::string, std::unordered_map<std::string, ParamEntry>> params_;

    void parseSection(const std::string& section);
};
