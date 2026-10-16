// PartDB.h
// 4주차: 부품 정보(parts.json)와 재고(inventory.json)를 읽어 품번으로 찾아 준다. 순수 C++ + nlohmann/json.
// 5주차(새 안): 재고에 공급사·공급일자·담당자·연락처(교수님 그림의 항목)를 더했다. 없는 항목은 빈 글자.
//
// 품번 규칙(데이터 설계): 3D 모델 노드 이름 "EV-BM-12S@03" → '@' 앞(EV-BM-12S)이 품번, 뒤(03)는 위치.
// 그래서 같은 품번 여러 개(모듈 9개, 셀 270개, 바퀴 4개)는 JSON 에 한 번만 적는다.
// 글자는 모두 UTF-8 그대로 둔다. MFC 컨트롤에 넣을 때 CA2W(문자열, CP_UTF8)로 바꾼다.
#pragma once
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

struct PartInfo
{
    std::string number;          // 품번 예: EV-BM-12S
    std::string name;            // 이름 예: 배터리 모듈
    std::string category;        // 분류 예: 배터리
    std::string desc;            // 한두 줄 설명
    std::string source;          // 3D 모델 출처
    int level = -1;              // 0 = L0 …
    bool hv = false;             // 고전압 부품
    std::vector<std::pair<std::string, std::string>> specs;   // 사양(파일에 적힌 순서 그대로)
    std::vector<std::string> cautions;                       // 주의사항
};

struct StockInfo
{
    enum class Status { Out, Low, Enough };   // 품절 · 부족 · 충분

    int stock = 0;               // 재고 수량
    int safetyStock = 0;         // 안전재고(이보다 적으면 부족)
    std::string location;        // 창고 위치 예: B-03-2
    long long price = 0;         // 단가(원)
    int leadTimeDays = 0;        // 주문하면 들어오는 데 걸리는 날
    std::string supplier;        // 공급사 예: (주)OO배터리
    std::string supplyDate;      // 마지막 공급일자 "YYYY-MM-DD"
    std::string manager;         // 담당자 이름
    std::string phone;           // 담당자 연락처

    Status GetStatus() const;    // 0 이면 품절, 안전재고 미만이면 부족, 그 외 충분
};

class PartDB
{
public:
    // 두 파일을 읽는다(경로는 UTF-8). 실패하면 false 와 이유(영문). 읽은 쪽은 그대로 쓸 수 있다.
    bool Load(const std::string& partsPath, const std::string& inventoryPath, std::string* error);
    bool Loaded() const { return !m_parts.empty(); }

    // 노드 이름("EV-BM-12S@03", "EV-BM-12S.001")도 받는다 → 품번 EV-BM-12S 로 찾는다. 없으면 nullptr.
    const PartInfo* FindPart(const std::string& nodeName) const;
    const StockInfo* FindStock(const std::string& nodeName) const;
    static std::string PartNumberOf(const std::string& nodeName);

    // 5주차 검색: 노드 이름(품번@위치)·이름·분류를 한 줄로 이어 놓고, query 의 낱말이 모두 들어 있으면 true.
    // 영문은 대소문자 무시("bm-12s" = "EV-BM-12S"). 빈 query 는 false. 둘 다 UTF-8.
    //   예) "모듈" → 배터리 모듈 9개 + BMS(배터리 제어 모듈),  "모듈 03" → EV-BM-12S@03,  "msd" → 서비스 플러그
    bool Matches(const std::string& nodeName, const std::string& query) const;

    size_t PartCount() const { return m_parts.size(); }
    size_t StockCount() const { return m_stock.size(); }

private:
    std::unordered_map<std::string, PartInfo> m_parts;
    std::unordered_map<std::string, StockInfo> m_stock;
};
