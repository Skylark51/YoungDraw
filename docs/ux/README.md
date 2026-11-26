# YoungDraw UX Auditor

Python 3 표준 라이브러리만 사용하는 런타임 목록 및 명시적 접근 경로 분석 도구다. 테스트 창을 실행하지 않는다. UI 자료 생성은 반드시 `scripts/test-isolated.ps1`로 실행한 `ui_access_validation`을 사용한다.

```powershell
python scripts/ux-auditor.py --evidence-dir build/december-ux-evidence/after `
  --output-dir build/ux-audit --graph docs/december-ux/graph-after.json `
  --workflows docs/december-ux/workflows.json --constraints docs/december-ux/constraints.json `
  --fail-on-constraints
```

입력은 `menus.csv`, `commands.csv`, `hits.csv`, `captures.csv`다. 스키마는 `tests/ui_access_validation.cpp`의 헤더를 그대로 사용한다. BOM을 허용하며 잘못된 열, 중복 JSON 키, 음수 비용·비유한 값, 존재하지 않는 캡처/노드 참조는 오류로 처리한다. 메뉴·검색·히트에 등장하는 동일 ID는 하나로 합치고 경로는 보존한다. 문자열 리터럴 수를 기능 수로 취급하지 않는다. 그래프 내부 자식 버튼 등 `hits.csv`에 없는 기능은 자동 완전 등록을 주장하지 않는다. 아이콘 의미와 선택 상태별 활성화 조건은 별도 조사 대상이다.

## 출력

| 파일 | 의미 |
| --- | --- |
| feature_registry.json | 런타임 ID, 명칭, 메뉴·검색·히트 경로와 관측 근거 |
| ui_graph.json | 관측 상태, 방향성 경로, 비용 성분 및 선택적 Fitts 지표 |
| workflow_scenarios.json | 명시한 시작·경유·도착 상태별 최단 경로 |
| affinity_matrix.csv | 도달 가능한 모델 경로에 공동 등장한 명령의 대칭 가중치 |
| interaction_cost.csv | 경로별 비용·가중치·도달 가능 여부 |
| ux_baseline.json | 입력 SHA-256, 개수, 모델 범위 및 미측정 지표 |
| ux_constraints.json | 명령 존재·모델 비용 상한의 검사 결과 |
| ux_report.html | 외부 통신·스크립트 없이 열 수 있는 자체 HTML 보고서 |

## 모델 계약

선택적 `--graph` JSON은 `nodes`, `edges`, `weights`, `fitts`를 받는다. 사용자 노드는 `{"id":"appearance","label":"서식 탭"}` 형태다. 런타임 명령은 `command:133`, 캡처는 `capture:graph-page1-800x600-requested96`처럼 등록된다. `command:`와 `capture:` 접두사를 사용자 노드로 재정의할 수 없다.

간선 예시:

```json
{"from":"appearance","to":"command:133","costs":{"actions":1},"evidence":"소스 및 캡처 근거 또는 명시적 가정"}
```

비용 성분은 `actions`, `key_presses`, `context_switches`, `penalty`이며 기본 가중치는 각각 1이다. `weights`로 보정한다. 출시 계획의 단축키 0.5, 휠 1.5, 대화상자 추가 2, 잘못 진입 후 복귀 3은 보정값 또는 `penalty`로 명시할 수 있다. 실측 시간으로 보정하기 전에는 가중 모델 단위다. 기본 메뉴 깊이 및 히트 1회 경로는 기능 진입 모형이며 작업 완료를 증명하지 않는다. 검색어 입력 비용은 자동 추정하지 않는다.

선택적 `pointer`는 `{"distance":300,"width":32}`다. Fitts 지표 `log2(1+D/W)`를 계산하며 계수가 없으면 초 단위 시간을 만들지 않는다. `fitts`에 `a`, `b`, `weight`를 모두 지정한 경우에만 예측 시간 `a+b*ID`를 비용에 더한다. 화면 간 좌표계를 섞거나 단위를 임의로 합치지 않도록 모델 작성자가 이동 거리와 계수를 검증해야 한다.

`--workflows`는 `{"workflows":[{"id":"style","start":"appearance","goal":"command:133","via":[],"frequency":1,"evidence":"소스 기반 모델"}]}` 형태다. `via`는 순서가 있는 경유 노드다. `frequency`는 기본 1의 모델 가중치이며 관측 사용자 빈도가 아니다. Dijkstra는 음수 비용을 허용하지 않는다. 도달 불가능한 경로는 비용을 null로 남긴다.

`--constraints`는 `required_command_ids` 정수 배열과 `max_workflow_cost`의 workflow ID → 상한을 받는다. 예시는 `docs/december-ux/constraints.json`에 있다. 알 수 없는 workflow ID는 입력 오류다. `--fail-on-constraints`는 실패 또는 검증 불가능한 제약이 있으면 종료 코드 1, 입력 오류는 2, 정상은 0을 반환한다.

좌표 고정·승인된 이동 제약은 별도의 `compare-ui-evidence.py --constraints`를 사용한다. 두 도구의 제약 스키마는 서로 다르다. 비교 도구의 정확한 `version:1`, `approved_moves`, `locked`, `forbidden` 계약은 해당 스크립트 문서 문자열을 참고한다.

## 범위와 재현

첫 클릭 성공률, 탐색 시간, 아이콘 인식률, 만족도, 실제 마우스 이동 감소율은 인간/조작 실측 없이는 계산하지 않는다. 합성 DPI 요청과 실제 DPI를 구분한다. 연관성 CSV는 실제 사용 빈도 기반 인접성 최적화 결과가 아니다. 전체 프로그램 기능 수나 전체 사용자 경험 점수로 해석하지 않는다.

`build.ps1`은 Python 단위 검사와 런타임 감사 fixture를 기본 검사에 포함한다. 전체 CTest도 반드시 `scripts/test-isolated.ps1`로 실행한다. GitHub Actions 산출물 `YoungDraw-ux-audit`에서 HTML·JSON·CSV를 확인할 수 있다. 이번 전후 모델과 캡처는 [12월 1차 결과](../december-ux/README.md)에 있다.
