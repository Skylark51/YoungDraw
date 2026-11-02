# 실험실 템플릿과 텍스트 편집 개선

상단 **리간드·배위 착물·기질·산·시약** 버튼이나 오른쪽 **템플릿**으로 그림 갤러리를 엽니다. 분류 버튼과 이름 검색으로 찾고, 그림을 누르면 오른쪽에 확대된 구조가 표시됩니다. 금속 선택 결과도 미리 볼 수 있습니다. 삽입 버튼·더블클릭·Enter로 넣고, 닫기·Esc로 취소합니다. 기존 그림이 있으면 그 오른쪽에 간격을 두고 넣습니다. 구조는 이미지가 아니라 원자·결합으로 편집됩니다. 152개 템플릿이 실행 파일 안에 포함되어 별도 Python/RDKit 설치가 필요 없습니다. 사용자 템플릿은 기존 `%LOCALAPPDATA%/YoungDraw/Templates`에 보관합니다.

## 리간드 7종

## 산소·질소 산화물 모티프 개선

side-on peroxo/hydroperoxo는 O–O를 44 pt, 두 M–O를 약 44.8 pt로 조정해 정삼각형에 가까운 도식으로 바꿨습니다. end-on은 M–O를 46 pt에서 34 pt로 줄였습니다. 이 값은 가독성을 위한 작도 비율입니다.

기존 7개 리간드 × axial/equatorial 방향에 다음 4종을 추가했습니다. 총 56개가 늘어 현재 152개 템플릿을 제공합니다.

- **{MNO}6 · nitrosyl**: 일반 금속 M에 N을 통해 NO를 연결한 직선형 작도 초안.
- **nitrite · nitro (N-bound)**: M–N(O)O.
- **nitrite · nitrito (O-bound)**: M–O–N–O.
- **nitrite · bidentate (O,O)**: 두 산소를 통해 M에 연결한 네 원자 고리.

검색창에서 `nitrosyl`, `MNO6`, `nitro`, `nitrito`, `bidentate`로 찾습니다. 금속 표시는 모두 **M**이며, 원자·결합·위치·전하를 직접 편집할 수 있습니다. MPDAP의 두 자리 nitrite에는 기존 side-on 모티프와 같은 kappa4 골격을 사용합니다.

{MNO}6는 요청하신 {FeNO}6 유형의 표현을 위한 이름이며, 임의의 M에 대해 전자 수·산화수·스핀을 계산했다는 의미는 아닙니다. NO와 nitrite의 표시 결합 차수·전하는 편집 가능한 도식 표현입니다. 실제 계의 확정된 전자 구조를 대신하지 않습니다. 참고: [nonheme {FeNO}6 구조 연구](https://pubs.rsc.org/en/content/articlehtml/2018/sc/c8sc01962b), [nitrite의 결합 형태 연구](https://pubs.rsc.org/en/content/articlehtml/2021/sc/d1sc02846d).

외부 연구 원본 없이 템플릿을 갱신하려면 `tools/refresh_coordination_templates.py`를 실행합니다. `tools/coordination_motifs.py`의 좌표 정의를 전체 재생성기에서도 공유합니다. `tools/test_coordination_templates.py`에서 모든 모티프의 연결 형태·거리·일반 금속 표기를 검사합니다.

### 원본 리간드 목록

| 이름 | 연결 구조 기준 | 분자식 |
|---|---|---|
| TBDAP | Youngcraft `tbdap-candidate.cjson`. 기존 후보의 연결 구조를 N,N′-di-tert-butyl pyridinophane 골격과 대조 | C22H32N4 |
| MDAP | Youngcraft `mdap.cjson`의 원본 Gaussian 구조 | C16H20N4 |
| Me3-TPADP (12-MTP) | Youngcraft `12-mtp.cjson`의 사용자 제공 Mn 구조 | C14H24N4 |
| 14-MTP | Youngcraft `14-mtp.cjson`의 사용자 제공 scan XYZ 구조 | C16H28N4 |
| CHDAP | 확인된 MDAP의 두 N-methyl기를 N-cyclohexyl기로 치환한 연결 구조. 입체 그림은 생성한 시작 배치 | C26H36N4 |
| MPDAP | `AfterPhD/6. Mn(IV)_Dichloride/MPDAP(2-3-4).cdxml`의 자유 리간드 #114350 | C21H23N5 |
| Me2S-DPADP | Youngcraft `me2s-dpadp.cjson`의 원본 Gaussian 구조 | C13H21N3S |

평면 리간드는 연결 구조를 유지하며 2D 도식을 생성했습니다. MPDAP 평면도와 원본 Mn(II)-Cl 입체도 #114427은 사용자가 그린 좌표를 재사용했습니다. 원본 CDXML 착물은 별도 분류에 그대로 보관하며, 생성한 MPDAP 착물에는 자유 리간드와의 그래프 대조를 거쳐 생략된 피리딘 이중결합을 복원했습니다.

Youngcraft의 원본 파일명·해시·연결 정보는 해당 프로젝트의 `lab_fragments/ligands`와 `docs/UPDATE_20261019.md`에서 확인했습니다. 각 템플릿의 출처, 분자식, SMILES, 파일 해시는 [manifest](../resources/lab_templates/manifest.json)에 있습니다.

명칭·치환기 대조 자료:

- [DGIST: N,N′-dicyclohexyl/di-tert-butyl/dimethyl pyridinophane 명칭](https://scholar.dgist.ac.kr/handle/20.500.11750/10715)
- [Co–TBDAP/MDAP peroxo 논문, JACS 2024](https://pubs.acs.org/doi/10.1021/jacs.4c03211)
- [Me3-TPADP peroxo/hydroperoxo 원 논문 공개본](https://www.osti.gov/servlets/purl/1361127)

## 배위·산소 그림

각 리간드에 대해 axial/equatorial **표현 방향**별로 배위 골격, peroxo의 side-on/end-on, hydroperoxo의 side-on/end-on을 제공합니다. 둘은 시점·도식 배치의 구분이며 실제 이성질체의 존재를 단정하지 않습니다. M은 일반 금속 자리이고 삽입 시 Fe/Mn/Co/Ni/Cu/Zn으로 고를 수 있습니다. 전하·산화수·스핀은 자동으로 지정하지 않습니다.

Youngcraft의 3D 좌표를 투영하여 주개 연결과 고리 연결을 유지하고, 뒤쪽에 해당하는 원자·결합은 `#a2a9b2`로 표현했습니다. CHDAP 입체도는 생성한 시작 배치를 사용합니다. 겹침이 적은 시점을 탐색하며, 원본 MPDAP의 쐐기·해시·회색·굵은선은 유지합니다.

MPDAP의 end-on 골격은 주개 N 5개를 유지합니다. side-on 산소의 두 자리를 표시하는 템플릿에는 **kappa4**를 명시하고 곁가지 피리딘 N의 금속 결합 하나를 해제했습니다. 이는 배위 모드를 수정하기 위한 도식 초안이며 실험적으로 확인한 화합물 목록이 아닙니다. O–O와 O–H도 편집 가능한 일반 결합입니다.

**편집 → 선택 부분을 뒤쪽으로 (옅은 회색)** 또는 **앞쪽으로 (검정)**으로 표시를 바꿀 수 있습니다. 원자·결합 색은 native/CDXML/SVG에 유지됩니다.

## 기질·산·시약

기질 12종: 2-PPA(원본 CDXML #27728), benzaldehyde, acetophenone, cyclohexanone, cyclohexanecarboxaldehyde, styrene, cyclohexene, cyclooctene, thioanisole, PPh3, MeCN, benzonitrile.

산·시약 6종: HOTf(TfOH), AcOH, HClO4, HBF4, H2O2, H2O. 산성 O–H는 명시적으로 그렸습니다. HBF4는 H+와 BF4−로 구분합니다.

### 템플릿 표시 개선 (2026-10-27)

Benzaldehyde와 cyclohexanecarboxaldehyde는 aldehyde의 C–H 결합과 H를 명시합니다. MeCN의 nitrile 탄소는 C 라벨로 표시합니다. HClO4는 중성 HO–Cl(=O)3 구조식으로 표시하며, HBF4는 H+를 충분히 떨어뜨리고 B의 음전하를 결합이 없는 위쪽에 둡니다. BF4−와 HOTf의 CF3에는 쐐기·해시를 사용하며, S와 Cl 주위도 십자형을 피하도록 좌표를 조정했습니다. 이 산·시약의 배치는 구조 정리 시 유지됩니다.

전체 내장 템플릿에서 N은 파란색(`#0000ff`), S는 읽기 쉬운 노란색(`#d4aa00`)으로 통일합니다. 착물의 뒤쪽 결합을 나타내는 회색은 유지합니다. 기본 cyclohexane 의자형은 두 꺾임과 평행한 맞은편 결합을 갖는 좌표로 바꾸고 앞쪽 결합을 굵게 표시합니다.

`tools/template_presentation.py`를 전체 생성기와 갱신기가 함께 사용합니다. 재생성해도 이 표시 규칙이 유지됩니다. `--export-lab-templates`는 기본 의자형의 `chair.png`와 `chair.svg`도 출력합니다.

## 구조 정리 보호

명시적으로 보호한 원자, 전이금속을 포함한 성분, 배위결합, 쐐기·해시, 옅은 회색을 포함한 성분은 정리할 때 **좌표를 변경하지 않습니다**. 투영의 비균일 결합 길이와 의도적인 왜곡도 유지합니다. 같은 문서의 보호되지 않은 일반 유기 구조는 기존 규칙으로 정리합니다. 템플릿 삽입·복사·저장 후에도 보호 속성이 유지되며, 일반 CDXML로 가져온 금속 착물도 금속 검출로 보호합니다.

## 텍스트

새 독립 텍스트는 클릭한 중심점을 기준으로 가운데 정렬하며, 여러 줄도 각 줄을 같은 중심에 맞춥니다. 기존 문서의 왼쪽 좌표는 불러올 때 보존하고 직접 편집 후에는 현재 그림의 중심을 유지하며 가운데 정렬로 바꿉니다.

입력 중 문서·전체 캔버스를 매번 다시 만들지 않고 확정할 때만 반영합니다. 부모 창은 자식 편집 상자 영역에 그리지 않으며, 편집 상자는 글자가 공간을 넘을 때만 여유를 확보하여 확장합니다. Enter/바깥 클릭은 확정, Shift+Enter는 줄바꿈, Tab/Shift+Tab은 필드 이동, Esc는 취소입니다.

## 검증과 재생성

- `tools/prepare_lab_templates.py`: 원본 추출·연결 검증·개발 시점 2D/투영 생성·내장 데이터 재생성. Python, RDKit, NumPy 필요.
- `YoungDraw --export-lab-templates build/lab-template-previews`: 실제 앱 렌더러로 PNG/SVG/CDXML 생성.
- 모든 152개 템플릿의 native/일반 CDXML 원자·결합 수, 회색 보존을 검사합니다.
- 모든 127개 배위 템플릿에서 정리 전후 native 문서가 동일함을 검사합니다. 결합만 선택한 경우와 복사·삽입 후에도 검사합니다.
- 여러 줄 텍스트의 공통 중심과 정렬 저장을 검사합니다.
- 실제 Windows UI에서 가운데 정렬 입력·여러 줄 확정, 분류·리간드 목록과 삽입을 확인했습니다.

검증 그림은 `build/lab-template-previews/`, 수정 전 소스는 `build/before-lab-templates.zip`에 보관했습니다.


## 회전·반전

상단의 좌우/상하 반전, ↶90°/↷90°, 각도 회전 버튼 또는 편집 메뉴를 사용합니다. 각도 창은 입력 결과를 미리 반영하며 취소하면 원본을 복원합니다. +는 시계방향, −는 반시계방향입니다.

원자·결합 선택은 해당 연결 성분 전체로 확장하여 분자를 강체로 변환합니다. 여러 구조를 함께 선택하면 공통 중심을 기준으로 처리합니다. 중심은 선택한 원자·객체 점의 평균으로 고정되므로 회전과 역회전을 하면 위치가 복원됩니다. 결합 길이·고리 모양·보호 속성·회색은 유지합니다. 반전에서는 쐐기/해시를 교환하고 명시적인 이중결합 선의 좌우 위치도 반전합니다. 원자 라벨과 선택한 독립 텍스트는 읽기 편하게 똑바로 유지합니다. Ctrl+Z / Ctrl+Shift+Z를 지원하며 변환 후 화면을 맞춰 잘리지 않게 표시합니다.

반응 블록이나 그래프의 전체 레이아웃은 회전하지 않습니다. 내부에서 회전할 구조의 원자나 결합을 선택하세요. 이미지·사각형·타원은 이 구조 변환의 대상에서 제외합니다. 선택한 자동 연결 화살표는 현재 모양을 고정한 뒤 함께 변환하며 연결을 해제합니다.

회전 검사: 90°와 역회전, 37.25°와 역회전, 내부 원자 거리 보존, 반복 반전 복원, 쐐기/해시·보호 속성·회색 보존, 비선택 분자 고정, 실행 취소, 텍스트·화살표 변환.

## 입체 결합 두께

기본 일반선 2, 굵은 골격선 3.2를 사용합니다. 쐐기의 넓은 끝은 BoldWidth 전체 폭을 사용해 굵은 골격선과 연결됩니다. 기존 문서의 폭 설정은 유지하며, 이전처럼 쐐기 폭을 두 배로 확대하지 않습니다. 짧은 배위 결합은 보이는 길이에 맞춰 쐐기 폭을 제한합니다. 해시는 일반선의 60% 두께와 평평한 끝으로 그리며, 짧은 결합에서는 두께와 간격을 함께 조정합니다. 앞쪽 이중결합은 골격선만 굵게, 안쪽 선은 일반 두께로 표시합니다. 이 규칙은 모든 구조의 화면·갤러리·PNG·SVG 렌더링에 공통 적용하며 원자 좌표, 쐐기 방향, 회색 뒤쪽 결합을 보존합니다.

검증: 폭·짧은 결합·해시 캡·고리 이중결합 회귀 검사와 core_tests/office_roundtrip 통과. 152개 템플릿을 전후 출력해 원자 좌표·결합·입체 표기·색상이 동일함을 확인했습니다. 전후 비교: `build/mpdap-depth-comparison.png`.

## 그래프 y축 눈금

그래프를 선택하고 **그래프·축 설정 → y축 눈금 간격 (0: 자동)** 에 5, 10, 2.5 등 간격을 입력합니다. 0 또는 빈 값이면 데이터 범위에 따라 1·2·2.5·5·10의 배수로 자동 결정합니다. 축의 양 끝도 눈금 간격의 배수에 맞추며 모든 경로의 데이터를 포함합니다. 설정은 그래프마다 native 및 smart CDXML에 저장되며, 이전 파일은 자동 모드로 열립니다. 설정 창에서 즉시 미리보기, 취소 복원, 실행 취소를 지원합니다. 지나치게 작은 간격은 설정 단계에서 안내하며, 이후 데이터 범위가 크게 늘어날 때는 지정 간격의 배수로 눈금을 묶어 과도한 눈금 생성을 방지합니다.

검증: 자동/고정/소수 간격, 음수·상수·작은 범위, 추가 경로 범위, 밀도 제한, 라벨 정밀도, native/CDXML 저장 복원과 이전 문서 호환, 실제 Windows 설정 창의 5 단위 미리보기와 적용을 확인했습니다.
