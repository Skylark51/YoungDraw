# YoungDraw 편집기 모듈

Windows 실행 파일은 `winmain.cpp`에서 시작하며 명령줄 처리와 앱 초기화를 맡습니다. 편집기 구현은 CMake의 `youngdraw_editor` 라이브러리에서 빌드하며 앱과 Windows UI 회귀 테스트가 함께 링크합니다. 아래 표의 이름은 `src/win_editor_<이름>.cpp`와 대응 헤더를 가리킵니다.

| 모듈 | 수정 범위 | 확인할 테스트 |
| --- | --- | --- |
| `window` | Win32 메시지, 창 수명, 입력 이벤트 전달 | `document_tabs`, `appearance_themes` |
| `state` | 활성 문서, 선택, 실행 취소 기록, 좌표 변환, 장면 캐시 | `document_tabs`, `animated_gif` |
| `commands` | 메뉴와 툴바의 명령 ID 및 기능 호출 | `document_tabs` |
| `shortcuts` | 키보드 동작, 반복 실행, 정렬과 이동 단축키 | `document_tabs` |
| `pointer` | 클릭, 히트 테스트, 선택·드래그, 원자·결합 그리기 | `document_tabs`, `core_tests` |
| `chrome` | 메뉴, 아이콘, 버튼, 툴팁, 테마 적용 | `appearance_themes` |
| `canvas` | 화면 합성, 도구 미리보기, 선택과 가이드 표시 | `appearance_themes`, `animated_gif` |
| `render` | 장면 그리기, PNG 미리보기, Office용 메타파일 | `appearance_themes`, `office_roundtrip` |
| `images` | 이미지 디코딩, GIF 프레임과 캐시 | `animated_gif` |
| `files` | 파일 선택, 읽기, 원자적 쓰기 진입점 | `atomic_file_save`, `document_tabs` |
| `clipboard` | 구조·이미지 복사/붙여넣기 형식 판별 및 삽입 | `animated_gif`, `office_roundtrip` |
| `tabs` | 문서 열기·저장·전환·닫기, 프로젝트 UI 연결 | `document_tabs`, `project_management` |
| `templates` | 템플릿 목록·저장, 즐겨찾기와 그룹 | `document_tabs` |
| `text` | 인라인 입력, 조합 입력, 텍스트 색상 적용 | `document_tabs` |
| `structure` | 선택 구조 변환과 정리, 그룹, 단계 구조 편집 | `document_tabs`, `core_tests` |
| `reaction` | 메커니즘 편집 대화상자, 단계 연결과 조작 | `mechanism_regression`, `mechanism_editing`, `mechanism_graph_editing`, `document_tabs` |
| `plot` | 에너지 그래프 설정, 단위·색상·계열 편집과 스냅 | `graph_energy_units`, `plot_layout`, `document_tabs` |

## 병렬 수정 기준

1. 브랜치마다 기능 모듈을 하나씩 맡고 각 구현 파일에서 변경을 시작합니다. 메뉴 항목이나 단축키를 추가할 때는 명령 ID와 `commands`, `chrome`, `shortcuts`의 변경 범위를 먼저 맞춥니다.
2. 다른 모듈에서 호출할 동작은 담당 모듈의 헤더에 선언합니다. 활성 문서의 변경은 기존 `history.checkpoint(...)`와 `changed()` 경로를 따르고, 화면 표시만 바뀌는 경우에는 `repaintOnly()`를 사용합니다. 편집기 상태는 Win32 UI 스레드에서 공유합니다.
3. 파일 형식은 `cdxml.cpp`와 `cdx.cpp`, 프로젝트 저장은 `win_projects.cpp`와 `win_project_manager.cpp`에서 수정합니다. 화학 모델과 배치는 `model.cpp`, `layout.cpp`, `scene.cpp` 및 `mechanism_*.cpp`, `plot_data.cpp`에서 관리합니다.
4. Windows에서 담당 테스트를 먼저 실행한 뒤 전체 회귀 테스트를 확인합니다. `build.ps1`은 전체 빌드와 테스트를 실행하며, GitHub의 Windows 빌드 워크플로는 `main` 병합 후 검증된 실행 파일을 갱신합니다.
