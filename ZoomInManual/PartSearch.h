// PartSearch.h
// 5주차(새 안): 화면 ③ 아래 검색 한 줄. 순수 C++(MFC·OpenGL 과 상관없음).
//   - 품번(@위치 포함)·이름·분류·증상(7주차 3차)에서 찾는다(PartDB::Matches: 영문 대소문자 무시, 낱말 여러 개는 모두 들어 있어야).
//   - 같은 검색어로 다시 찾으면 다음 부품(끝이면 처음으로). 엔터를 누를 때마다 다음 부품으로 이동하는 데 쓴다.
//   - 순서: 큰 부품(단계가 낮은 것) 먼저, 같은 단계는 모델 순서.
//     예) "배터리" → 배터리팩(L1) → 모듈·BMS…(L2) → 셀(L3),  "모듈 03" → EV-BM-12S@03 하나
#pragma once
#include "Model.h"
#include "PartDB.h"

#include <string>
#include <vector>

class PartSearch
{
public:
    // query(UTF-8)에 맞는 부품 노드 번호. 지난번과 같은 검색어면 다음 부품, 새 검색어면 첫 부품. 없으면 -1.
    int Next(const Model& model, const PartDB& db, const std::string& query);
    void Reset();                                        // 다음 검색은 첫 부품부터

    int Count() const { return static_cast<int>(m_matches.size()); }   // 맞은 부품 수
    int Index() const { return m_index; }                // 지금 고른 것이 몇 번째인지(0부터), 없으면 -1

private:
    std::string m_key;            // 지난 검색어(앞뒤 공백 제거·소문자·공백 하나로)
    std::vector<int> m_matches;   // 맞은 부품 노드(정렬된 순서)
    int m_index = -1;
};
