# 부분 선택 직접 회전

직접 회전 도구는 명시적으로 선택한 원자와 결합의 끝점만 회전한다. 선택 영역 안의 결합을 잡거나 선택한 결합의 끝점을 잡아도 전체 분자로 확대하지 않는다. 선택하지 않은 연결 원자와 그룹의 다른 원자는 고정되며, 경계 결합은 유지된다.

선택 없이 구조를 잡으면 기존처럼 연결된 분자 전체를 회전한다. 기존 90° 회전·각도 입력·반전 명령의 범위와 아이콘 위치는 유지한다. 선택 중심, Shift 15° 스냅, Esc·캡처 상실 취소, 한 단계 실행 취소·다시 실행도 유지한다.

`pointer_quality_tests`는 실제 편집기 명령과 마우스 메시지로 연결된 4원자 구조의 2원자만 회전한다. 원자 선택·결합 선택·선택한 원자 사이 결합을 잡는 경우를 그룹 유무별로 검사한다. 선택 밖 좌표, 결합 종류·그룹 등 나머지 문서 데이터 보존과 뷰포트 고정, 취소, 무이동, 실행 취소·다시 실행을 검증한다. 기존 전체 분자 회전 검사도 유지한다.

같은 새 회귀 검사를 수정 전 코드에 실행했을 때 `direct rotation uses the selected fragment center`에서 실패하여 문제를 재현했다. 로그는 `build/fragment-rotation-before/isolated-ctest.log`에 보관한다. 모든 UI 실행은 `scripts/test-isolated.ps1`을 통한 별도 데스크톱에서 수행한다.

독립 재현 명령:

```powershell
./scripts/test-isolated.ps1 -BuildDirectory build/fragment-rotation `
  -Executable build/fragment-rotation/pointer_quality_tests.exe `
  -Arguments '--partial-rotation-only'
```
