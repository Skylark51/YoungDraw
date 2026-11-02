# ChemDraw 단축키 대조 및 반영

Revvity의 [공식 단축키 안내](https://revvitysignals.com/sites/default/files/2023-11/rs-chemdraw-tips-and-tricks-eBook-11-17-23.pdf)와 [ChemDraw 사용자 설명서 5장](https://chem.beloit.edu/classes/programs/ChemDraw_21_manual.pdf)을 기준으로 대조했습니다. 아래는 YoungDraw에 반영한 동작입니다. F1과 배치 메뉴에서도 확인할 수 있습니다.

| 단축키 | YoungDraw 동작 |
|---|---|
| Ctrl+Shift+Alt+L / C / R | 선택 객체 왼쪽 / 가로 중앙 / 오른쪽 맞춤 |
| Ctrl+Shift+Alt+T / M / B | 선택 객체 위 / 상하 중앙 / 아래 맞춤 |
| Ctrl+Shift+Alt+H / V | 가로 / 세로의 빈 간격을 균등하게 배분 |
| Ctrl+R / Ctrl+K | 회전 / 크기 대화상자 |
| Ctrl+Shift+K | 구조 정리 미리보기 |
| Ctrl+Shift+X | YoungDraw 반응 배치·크기 정돈 |
| Ctrl+Shift+H / V | 좌우 / 상하 반전 |
| 방향키 / Shift+방향키 | 1 / 10 pt 이동 |
| Alt+좌우 / Alt+상하 | ±1° / ±15° 회전 |
| Ctrl+방향키 | 해당 방향에 구조 복제와 반응 화살표 추가 |
| Ctrl+G / Ctrl+Shift+G | 그룹 / 그룹 해제 |
| Ctrl+D | 선택 부분을 CDXML 텍스트로 복사 |
| Ctrl+Shift+Alt+P | 기존 CDXML·구조 클립보드 붙여넣기 |
| Ctrl+B / I / U / T | 선택한 독립 텍스트 전체의 굵게 / 기울임 / 밑줄 / 일반 서식 |
| Ctrl+Shift+L / C | 선택한 독립 텍스트 왼쪽 / 가운데 정렬 |
| F9 / F10 | 텍스트 입력 중 아래 / 위첨자. Ctrl+= / Ctrl+Shift+=도 유지 |
| Shift+G | 오비탈 도구 |
| 원자 위 a / 3 | 짧은 연결선으로 phenyl 추가 |
| 결합 위 a | 벤젠 고리 융합 |
| Alt+K | 선택한 탄소 방향족 고리의 Kekulé / 비편재화 표시 전환 |
| Space / x / j / e / t | 선택 / 단일결합 / 벤젠 / 화살표 / 텍스트 도구 |
| Ctrl+N / O / S / Shift+S / W | 새 문서 / 열기 / 저장 / 다른 이름 저장 / 탭 닫기 |
| Ctrl+Z / Ctrl+Shift+Z | 실행 취소 / 다시 실행 |
| Ctrl+Y | 마지막 정렬·이동·회전·반전·크기 변경 등의 작업 반복 |

**다시 실행은 Ctrl+Shift+Z로 변경했습니다.** Ctrl+Y는 ChemDraw의 작업 반복에 맞췄습니다. Ctrl+C/X/V/A는 복사·잘라내기·붙여넣기·전체 선택을 유지합니다. 문서 탭 전환은 Ctrl+Tab / Ctrl+Shift+Tab입니다.

정렬은 선택한 원자나 결합의 연결 분자 전체를 이동합니다. 한 분자의 원자 둘을 골라도 분자를 찢어 정렬하지 않습니다. 그룹, 반응 블록, 구조를 포함한 그래프는 각각 함께 이동합니다. 잠긴 레이어는 이동하지 않습니다. 간격 배분은 세 개 이상의 독립 객체에 적용하며, 모든 편집은 실행 취소할 수 있습니다.

## 지원 범위

ChemDraw의 이름↔구조 변환, SMILES/MOL 변환, 3D 최적화, Structure Perspective, 고급 텍스트 맞춤·부분 서식, 모든 닉네임과 기능기 자동 생성은 아직 구현되지 않았습니다. 해당 단축키를 새 문서·잘라내기·일반 복사 등 무관한 동작으로 처리하지 않도록 수정했습니다. 반응 정돈과 구조 복제는 YoungDraw의 문서 모델에 맞는 동작입니다.

기존 YoungDraw의 빈 공간 `a`는 공명 결합 도구로 유지합니다. 원자·결합 위에서는 ChemDraw의 phenyl·벤젠 융합 동작을 우선합니다.

## 벤젠 작도 기준

벤젠 고리 결합은 일반 C–C 기준의 **75%**로 줄였습니다. 이전 80% 대비 6.25% 작습니다. 벤젠 밖 탄소로 이어지는 단일결합은 **85%**입니다. 구조 정리에서도 carbonyl 탄소 등 바깥 탄소에 이중결합이 있다는 이유로 이 연결선을 일반 길이로 늘리지 않습니다. 새 작도·phenyl 단축키·2D 구조 정리가 같은 기준을 사용합니다.

기존 구조는 Ctrl+Shift+K에서 **2D 결합·고리 재정리**를 적용하세요. 원본 형태 유지 모드와 명시적으로 보호된 입체·금속 구조는 기존 투영을 보존합니다.
