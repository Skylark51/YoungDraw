# YoungDraw 종합 품질 개선 — 2026-11-16

## 범위와 판단 기준

C++17/Win32/GDI+ 기반 편집기, 모델·배치, 메커니즘·에너지 그래프,
CDXML/CDX, Office OLE, 프로젝트 저장, 문서 탭, 렌더링 및 기존 회귀 검사를 검토했다.
기존 기능과 그래프 개형을 유지하고 재현 가능한 결함을 보완했다.
ChemDraw/Origin 전체 기능과 동등한 상용 품질을 입증한 결과로 해석하지 않는다.

## 주요 문제 10가지와 수정 전후

| 번호 | 우선순위 / 난이도 | 확인한 문제 | 수정 결과 / 근거 |
| --- | --- | --- | --- |
| 1 | P0 / 낮음 | 손상된 네이티브 문서의 빈 텍스트 좌표·불완전한 곡선이 렌더러의 무검사 배열 접근에 도달할 수 있음 | 읽기 단계에서 잘못된 객체 형상을 거부한다. 텍스트·곡선·이미지 3종 오류 입력 검사 |
| 2 | P0 / 중간 | 일반 편집 문서의 주기적 복구 저장이 없었음. 프로젝트 노트 저장과 문서 복구는 별개 | 수정된 여러 탭을 1분마다 별도 원자적 복구 파일로 저장한다. 다음 실행에서 새 탭으로 복원하며 원본을 덮어쓰지 않는다. 활성 프로세스 잠금·손상 자료 보존 검사 |
| 3 | P0 / 낮음 | 일반 객체 드래그가 마지막 mouse-up 좌표를 반영하지 않음 | 모든 드래그가 마지막 위치를 반영한다. **기준 커밋 bd1cb98 빌드에서 신규 검사 실패, 수정 빌드에서 통과** |
| 4 | P0 / 낮음 | 결합만 선택하면 이동 대상으로 양끝 원자가 확장되지 않아 움직이지 않음 | 양끝 원자만 이동하고 다른 원자는 고정한다. 단일 결합 선택·경계 원자·Undo/Redo 검사 |
| 5 | P0 / 낮음 | 원자에 연결된 직선 화살표를 단독 이동하면 연결이 남아 원래 위치로 라우팅됨 | 단독 이동 시 현재 보이는 위치를 유지해 분리하고 이동한다. 곡선에 이미 있던 원칙을 직선 화살표에도 적용 |
| 6 | P0 / 중간 | Ctrl 복제 드래그 취소 후 복제 ID 선택이 남음. 탭 전환 시 진행 중 드래그가 다른 문서와 섞일 수 있음 | 취소·캡처 상실·WM_CANCELMODE를 같은 취소 경로로 처리한다. 복제 전 선택과 문서를 복원하며 탭 전환 전 미확정 드래그를 취소한다 |
| 7 | P1 / 중간 | 곡선의 빈 사각형 내부가 선택되고 여러 선분은 첫 선분만 검사함 | 실제 베지어·모든 선분·화살촉을 검사한다. 채운 곡선·도형·이미지의 내부 선택은 유지한다. 바운딩 박스로 후보를 먼저 걸러 대형 문서 비용을 줄인다 |
| 8 | P0 / 낮음 | 가져온 CDXML 원자의 색·명시적 수소 수 변경 및 이중결합 위치 초기화가 원문 패치에 누락됨 | 색·수소 수를 갱신하고 자동 수소·자동 결합 위치로 되돌린 속성을 제거한다. **스마트 메타데이터를 제거한 외부 CDXML 재가져오기**에서 검증 |
| 9 | P1 / 낮음 | 기존 `.ydraw` 문서 Ctrl+S가 기존 경로에 저장하지 않고 CDXML Save As를 요구함 | `.ydraw`는 원래 경로에 네이티브 형식으로 저장한다. CDXML 기본 새 저장 정책과 보존 원문은 유지한다. 숨김 창에서 실제 저장·재개방 검사 |
| 10 | P0 / 낮음 | SVG 도형의 점선·투명도가 화면 렌더러와 달리 빠짐 | 타원·사각형 점선과 Primitive 투명도를 출력한다. 기존 에너지 그래프 5종은 개선 전후 SVG 전체가 동일함 |

추가로 휠 입력의 실제 delta를 사용하여 여러 노치·고해상도 휠과 0 delta를 처리하고
커서 아래 좌표를 유지한다. 연결 성분 탐색은 인접 목록으로 바꾸어 각 방문 원자마다
전체 결합을 다시 훑던 경로를 줄였다. Undo/Redo 복원은 저장된 문서를 이동해
불필요한 깊은 복사를 줄였다. 처리 시간 향상 수치는 별도로 계측하지 않았다.

## 검증

- Windows / MSVC 19.44 / Release / `build.ps1` 사용.
- 전체 CTest 21/21 통과. 기본 백그라운드 빌드에는 새 검사 2개를 추가해 12/12 통과.
- `quality_regression`: 부분 결합 이동, 화살표 고정점, 80회 Undo/Redo,
  곡선·채움 선택, 일반 CDXML 속성, 잘못된 문서 형상, SVG 서식,
  3,000원자 연결 탐색과 대형 문서의 24회 Undo/Redo,
  Unicode 경로·여러 탭·잠금·손상 복구를 검사한다.
- `pointer_quality`: 실제 Win32 숨김 편집 창에서 mouse-up 최종 위치,
  한 번의 Undo, 탭 전환, 복제 취소, 캡처 상실, `.ydraw` 저장,
  커서 기준 휠 확대를 검사한다. 사용자 클립보드를 사용하지 않는다.
- 기존 프로젝트 관리·문서 탭·구조 정리·메커니즘 단계 편집·CDX·그래프 배치·GIF·테마
  회귀 검사를 유지하고 통과했다.
- 실제 연구 CDXML 24개: `november_validation <자료폴더> --source-import`로 원본
  가시 CDXML을 다시 파싱하여 수정·CDXML/네이티브 저장·복원을 통과했다.
  그중 20개는 메커니즘 변환·좌표 보존·분기 추가/제거도 통과했고,
  변환 조건이 없는 4개는 변환 성공 수에서 제외했다. 연구 원본은 변경하지 않았다.
- 기존 복잡한 에너지 그래프 5종의 기준/수정 빌드 SVG가 **바이트 단위 동일**하다.
  기본 그래프와 고정 개형 그래프의 PNG도 픽셀 단위 동일하다.
  메커니즘 SVG·네이티브 데이터도 동일하며 PNG는 798,000픽셀 중
  6픽셀의 래스터 차이가 있었다. 배치 변경으로 집계하지 않는다.
- `office_storage_roundtrip`: 실제 OLE 저장소·EMF, 편집 콜백, 저장·취소·재열기를 통과했다.
- 설치된 PowerPoint에서 **창 0개 / 슬라이드 6개**의 PPTX 저장·재개방을 수행했다.
  원본 YoungDraw 스트림 6개 SHA-256, OLE 표시 캐시 및 EMF 6개가 보존됐다.
- 실제 YoungDraw 실행 화면을 확인했다. 사용자의 데스크톱 조작이 감지되어
  이번 작업에서는 PowerPoint 복사·붙여넣기·더블클릭 UI 전체를 재실행하지 않았다.
  이전 UI 검증 기록은 `november-validation.md`에 있으며 이번 검증과 구별한다.

## 산출물과 재현

검증 파일은 Git에 포함하지 않고 `build/`에 보관한다.

- `build/quality-build.log`, `build/quality-all-tests.log`: 빌드/CTest 결과.
- `build/quality-real-cdxml.log`: 실제 CDXML 가져오기·편집 결과.
- `build/quality-comparison/`: 그래프 5종의 before/after SVG 및 기존 데모 PNG.
- `build/quality-powerpoint/reopened.pptx`: 실제 PowerPoint에서 재저장한 6슬라이드.
- `build/comprehensive-quality/office-storage-verification/`: OLE/EMF 및 재편집 원본.
- `build/quality-baseline-source/`: 기준 커밋 임시 빌드. 신규 pointer 검사를
  연결하여 실패를 재현했다. 소스 커밋에 포함하지 않는다.

```powershell
./build.ps1 -BuildDirectory build/comprehensive-quality
# 전체 테스트는 빌드 스크립트의 임시 드라이브가 유지되는 동안 실행해야 한다.
./build.ps1 -BuildDirectory build/comprehensive-quality -HeadlessTests:$false

./tools/test-powerpoint-background.ps1 -Stage Prepare `
  -SourceDirectory build/comprehensive-quality/office-storage-verification `
  -OutputDirectory build/quality-powerpoint
python tools/verify-powerpoint-package.py prepare `
  --sources build/comprehensive-quality/office-storage-verification --output build/quality-powerpoint
./tools/test-powerpoint-background.ps1 -Stage Reopen `
  -SourceDirectory build/comprehensive-quality/office-storage-verification `
  -OutputDirectory build/quality-powerpoint
python tools/verify-powerpoint-package.py `
  --sources build/comprehensive-quality/office-storage-verification --output build/quality-powerpoint
```

PowerPoint 스크립트는 다른 PowerPoint 프로세스가 있으면 중단한다.
시험 프로세스 재사용 옵션도 문서·창이 없는지 확인한다. 사용자의 앱을 강제 종료하지 않는다.

## 편집 호환성의 실제 범위

| 방식 | 지원 범위 |
| --- | --- |
| YoungDraw 안의 개별 객체 | 지원하는 CDXML 원자·결합·텍스트·화살표·곡선은 개별 편집 가능. 메커니즘 연결 추정은 편집 표에서 검토 필요 |
| CDXML 외부 편집 | 표준 화학 객체와 지원 서식을 내보낸다. 미지원 원문 객체·다른 페이지는 보존하지만 YoungDraw 화면/SVG에서 빠질 수 있음 |
| PowerPoint YoungDraw OLE | YoungDraw 편집 원본과 벡터 표시를 함께 저장한다. 더블클릭 재편집은 YoungDraw OLE 서버가 등록된 Windows 환경 필요 |
| PowerPoint SVG/EMF | 선명한 벡터 그림. 붙여넣기만으로 원자·결합이 PowerPoint 네이티브 화학 객체가 된다는 의미는 아님. 도형 변환도 원래 화학 의미/YoungDraw 재편집 보존을 보장하지 않음 |

## 남은 한계와 다음 우선 작업 5가지

1. ChemDraw 미지원 도형·다중 페이지·혼합 화살촉을 실제 ChemDraw 재개방까지 대조하는 fixture 확대.
2. 실제 PowerPoint 붙여넣기·더블클릭·호스트 저장 전체 흐름을 다양한 Office 버전에서 자동화.
3. 대형 문서의 장시간 드래그·장기 Undo 메모리·렌더링 시간을 계측하고 병목을 추가 개선.
4. 다중 모니터/125~200% 배율에서 메뉴·도킹 패널·선택 핸들의 실제 배율 전환 검증.
5. 자동 복구의 강제 중단/디스크 부족/대용량 이미지 시나리오 및 복구 목록 UX 확대.

자동 복구는 최대 1분의 미반영 구간이 있고 드래그·인라인 입력 도중에는 저장하지 않는다.
복구 묶음도 기존 원자적 저장의 64 MB 제한을 사용한다. 실패 시 상태줄에서
수동 저장을 안내하고 이전 복구 파일을 보존한다. 복구 문서는 새 탭의 미저장 사본이며
Undo 이력은 복원하지 않는다. 읽을 수 없는 복구 파일은 수동 복구를 위해 남긴다.

## 변경 파일

- 모델·선택·벡터: `src/layout.cpp`, `src/model.cpp`, `src/scene.cpp`, `src/scene.h`.
- 호환성: `src/cdxml.cpp`.
- 편집 상태·저장: `src/win_editor_pointer.cpp`, `src/win_editor_window.cpp`,
  `src/win_editor_tabs.cpp`, `src/win_editor_tabs.h`, `src/winmain.cpp`.
- 복구: `src/win_recovery.cpp`, `src/win_recovery.h`.
- 회귀/빌드: `tests/quality_tests.cpp`, `tests/pointer_quality_tests.cpp`,
  `CMakeLists.txt`, `build.ps1`.
- 검증 도구: `tools/test-powerpoint-background.ps1`, `tools/verify-powerpoint-package.py`.
