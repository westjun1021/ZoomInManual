// PartDB.cpp
#include "pch.h"

// Windows 헤더의 min/max 매크로가 nlohmann/json 과 부딪히지 않게 이 파일에서만 지운다.
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include "PartDB.h"

#include <nlohmann/json.hpp>

#include <cctype>
#include <filesystem>
#include <fstream>

namespace
{
    // specs 를 파일에 적힌 순서대로 보여 주려고 ordered_json 을 쓴다(그냥 json 은 이름순으로 바뀐다).
    using Json = nlohmann::ordered_json;

    // UTF-8 경로 → 파일 경로. Windows 는 넓은 문자로 바꿔야 한글 폴더 이름도 열린다.
    std::filesystem::path PathFromUtf8(const std::string& utf8)
    {
#ifdef _WIN32
        const int count = ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
        std::wstring wide(static_cast<size_t>(count), L'\0');
        if (count > 0)
            ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), &wide[0], count);
        return std::filesystem::path(wide);
#else
        return std::filesystem::path(utf8);
#endif
    }

    // 오류 글자는 영어로만 만든다(한글 문자열 상수는 MSVC 에서 CP949 로 바뀌어 UTF-8 로 읽으면 깨진다).
    bool ReadJson(const std::string& path, Json* out, std::string* error)
    {
        std::ifstream file(PathFromUtf8(path), std::ios::binary);
        if (!file)
        {
            if (error) *error += "cannot open " + path + "\n";
            return false;
        }
        try
        {
            *out = Json::parse(file);   // 맨 앞의 UTF-8 BOM 은 nlohmann/json 이 알아서 건너뛴다
        }
        catch (const std::exception& e)
        {
            if (error) *error += path + ": " + e.what() + "\n";
            return false;
        }
        if (!out->is_object())
        {
            if (error) *error += path + ": top level must be { \"part number\": { ... } }\n";
            return false;
        }
        return true;
    }

    std::string Text(const Json& value)
    {
        return value.is_string() ? value.get<std::string>() : value.dump();
    }
}

StockInfo::Status StockInfo::GetStatus() const
{
    if (stock <= 0)
        return Status::Out;
    // 안전재고까지 내려왔으면 이미 주문할 때 → 같을 때도 '부족'(7주차 2차. 전에는 미만일 때만)
    if (stock <= safetyStock)
        return Status::Low;
    return Status::Enough;
}

bool PartDB::Load(const std::string& partsPath, const std::string& inventoryPath, std::string* error)
{
    m_parts.clear();
    m_stock.clear();
    bool ok = true;

    Json parts;
    if (ReadJson(partsPath, &parts, error))
    {
        for (auto it = parts.begin(); it != parts.end(); ++it)
        {
            const Json& v = it.value();
            if (!v.is_object())
                continue;
            try
            {
                PartInfo info;
                info.number = it.key();
                info.name = v.value("name", it.key());
                info.category = v.value("category", "");
                info.desc = v.value("desc", "");
                info.source = v.value("source", "");
                info.level = v.value("level", -1);
                info.hv = v.value("hv", false);
                if (v.contains("specs") && v["specs"].is_object())
                {
                    for (auto s = v["specs"].begin(); s != v["specs"].end(); ++s)
                        info.specs.emplace_back(s.key(), Text(s.value()));
                }
                if (v.contains("cautions") && v["cautions"].is_array())
                {
                    for (const Json& c : v["cautions"])
                        info.cautions.push_back(Text(c));
                }
                if (v.contains("symptoms") && v["symptoms"].is_array())   // 7주차 3차(없어도 된다)
                {
                    for (const Json& c : v["symptoms"])
                        info.symptoms.push_back(Text(c));
                }
                const std::string key = info.number;
                m_parts[key] = std::move(info);
            }
            catch (const std::exception& e)   // 값 종류가 틀린 항목(예: "level": "L2")은 건너뛰고 알린다
            {
                if (error) *error += partsPath + ": " + it.key() + ": " + e.what() + "\n";
                ok = false;
            }
        }
    }
    else
    {
        ok = false;
    }

    Json stock;
    if (ReadJson(inventoryPath, &stock, error))
    {
        for (auto it = stock.begin(); it != stock.end(); ++it)
        {
            const Json& v = it.value();
            if (!v.is_object())
                continue;
            try
            {
                StockInfo s;
                s.stock = v.value("stock", 0);
                s.safetyStock = v.value("safetyStock", 0);
                s.location = v.value("location", "");
                s.price = v.value("price", 0LL);
                s.leadTimeDays = v.value("leadTimeDays", 0);
                s.supplier = v.value("supplier", "");
                s.supplyDate = v.value("supplyDate", "");
                s.manager = v.value("manager", "");
                s.phone = v.value("phone", "");
                m_stock[it.key()] = s;
            }
            catch (const std::exception& e)
            {
                if (error) *error += inventoryPath + ": " + it.key() + ": " + e.what() + "\n";
                ok = false;
            }
        }
    }
    else
    {
        ok = false;
    }
    return ok;
}

std::string PartDB::PartNumberOf(const std::string& nodeName)
{
    const size_t at = nodeName.find('@');
    if (at != std::string::npos)
        return nodeName.substr(0, at);
    // Blender 가 복제할 때 붙이는 ".001" 꼬리
    const size_t dot = nodeName.rfind('.');
    if (dot != std::string::npos && dot + 4 == nodeName.size() &&
        std::isdigit(static_cast<unsigned char>(nodeName[dot + 1])) &&
        std::isdigit(static_cast<unsigned char>(nodeName[dot + 2])) &&
        std::isdigit(static_cast<unsigned char>(nodeName[dot + 3])))
        return nodeName.substr(0, dot);
    return nodeName;
}

namespace
{
    std::string LowerAscii(std::string s)
    {
        for (char& c : s)
        {
            if (c >= 'A' && c <= 'Z')
                c = static_cast<char>(c - 'A' + 'a');
        }
        return s;
    }
}

bool PartDB::Matches(const std::string& nodeName, const std::string& query) const
{
    // 찾을 대상: "ev-bm-12s@03 배터리 모듈 배터리" (한글은 UTF-8 그대로 — 부분 문자열로 찾아도 글자가 깨지지 않는다)
    std::string haystack = nodeName;
    if (const PartInfo* info = FindPart(nodeName))
    {
        haystack += " " + info->name + " " + info->category;
        for (const std::string& symptom : info->symptoms)   // 7주차 3차: 증상 낱말로도("끽 소리" → 브레이크 패드)
            haystack += " " + symptom;
    }
    haystack = LowerAscii(haystack);

    bool anyWord = false;
    const std::string lowered = LowerAscii(query);
    size_t pos = 0;
    while (pos < lowered.size())
    {
        while (pos < lowered.size() && (lowered[pos] == ' ' || lowered[pos] == '\t'))
            ++pos;
        size_t end = pos;
        while (end < lowered.size() && lowered[end] != ' ' && lowered[end] != '\t')
            ++end;
        if (end > pos)
        {
            anyWord = true;
            if (haystack.find(lowered.substr(pos, end - pos)) == std::string::npos)
                return false;   // 낱말 하나라도 없으면 아님
        }
        pos = end;
    }
    return anyWord;
}

const PartInfo* PartDB::FindPart(const std::string& nodeName) const
{
    const auto found = m_parts.find(PartNumberOf(nodeName));
    return found == m_parts.end() ? nullptr : &found->second;
}

const StockInfo* PartDB::FindStock(const std::string& nodeName) const
{
    const auto found = m_stock.find(PartNumberOf(nodeName));
    return found == m_stock.end() ? nullptr : &found->second;
}
