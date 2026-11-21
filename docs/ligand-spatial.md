# BQCN·Co–Si 템플릿과 입체 변환

## 사용 방법

1. **삽입 → 템플릿**에서 BQCN 또는 MeSiP로 검색합니다. BQCN은 S,S / R,R, MeSiP₂는 P(iPr)₂ 치환기를 갖는 구조입니다.
2. 평면 구조, 입체 리간드, Co 배위 골격을 선택합니다. MeSiP₂는 Si–H 전구체, iPr 약어 착물, CO 표시 착물도 제공합니다. BQCN은 원본 Co–O₂ side-on 구조도 제공합니다.
3. 구조의 원자·결합을 선택하고 **편집 → 선택 구조 입체 변환·회전**을 엽니다. 메커니즘·그래프의 구조 영역도 선택할 수 있습니다.
4. 평면 구조는 자동 인식으로 기준 좌표를 적용합니다. 이미 입체인 구조는 **현재 입체 좌표 유지**가 기본입니다.
5. 보기 방향, 상하·좌우·화면 안 회전, 뒤쪽 회색 강도를 조절합니다. 숫자 옆 버튼·휠로 미리보기를 조절하고 적용합니다.
6. 취소는 원본을 복원합니다. 적용은 Ctrl+Z 한 번으로 되돌립니다. 다시 열어 회전할 수 있습니다.

입체도는 편집 가능한 원자·결합입니다. N 라벨의 색과 글자는 유지하고 결합만 깊이에 따라 회색으로 표시합니다. 회색 강도 0은 원래 결합 색입니다. 일반 2D 회전·이동·전체 크기 조절 후에도 입체 좌표를 사용할 수 있습니다. .ydraw / YoungDraw CDXML에는 깊이를 함께 저장하고 SVG·PNG·Office 표시에는 적용된 투영과 회색을 반영합니다. 외부 프로그램에서 CDXML을 수정하여 YoungDraw 상태 정보가 무효가 되면 표시용 2D 원자·결합으로 가져옵니다.

## 구조 기준

- **BQCN:** C26H28N4. 원본은 사용자의 Cobalt deformylation 연구 자료에 보관된 sideon_extracted.xyz이며, 3-without-2PPA-RC-opt-freq.out의 최종 구조를 추출한 자료입니다. 2026-11-12에 만든 미최적화 CH3CN/H2O 조합 구조를 기준으로 사용하지 않았습니다. 금속을 제거하고 독립된 BQCN 성분을 추출하여 원자·결합·입체 중심을 확인했습니다.
- **MeSiP₂:** MeSi[2-P(iPr)2-C6H4]2 골격. 원본은 Co–Si 연구의 Output/S=0 Real/ACoord.xyz입니다. Si에 연결된 Me와 두 aryl-P(iPr)2 팔을 확인했습니다. MeSiP₂ 리간드의 좌표는 원본 Co 배위 상태에서 추출한 형태이며 자유 리간드의 평형 구조를 새로 계산한 결과는 아닙니다.
- BQCN의 반대 거울상은 추출된 좌표의 반사로 만들고 각각 S,S / R,R로 구분합니다. 카메라 회전과 거울상 전환은 별개입니다.
- 출처와 SHA-256, 연결 구조 SMILES는 resources/ligand_templates/manifest.json에 보관합니다. 추출한 리간드 XYZ를 함께 보관합니다. 전체 연구 원본의 복사본은 Git에서 제외되는 build/ligand-spatial-sources에만 보관합니다.
- 참고 논문: [Metal–ligand cooperative transformation of alkyl azide to isocyanate occurring at a Co–Si moiety](https://doi.org/10.1039/D0CC08012H).

## 변환 범위

자동 변환은 BQCN·MeSiP₂의 확인된 원자·결합 연결을 기준 좌표에 대응시키는 기능입니다. 일반 분자의 3D 예측이나 전자구조 계산을 수행하지 않습니다. 기존 원자 번호·결합·전하·스핀 값과 메커니즘 연결은 유지합니다. Co의 산화수나 스핀은 자동 지정하지 않습니다. 금속에 붙은 추가 리간드는 기존 내부 모양과 부착 위치의 상대 이동을 유지하며, 기준에 없는 부분의 새 3D 형태를 예측하지 않습니다. 여러 기준 원자를 잇는 추가 가교는 자동 변환하지 않습니다.

XYZ에는 결합 차수가 없으므로 개발 단계에서 연결과 결합 차수를 검증하고 템플릿으로 내장합니다. 일반 사용 시 Python·RDKit이나 인터넷은 필요 없습니다.

## 개발 및 검증

생성기: tools/prepare_ligand_spatial_templates.py. 개발 환경의 RDKit·NumPy로 실행하며 --bqcn-source, --cosi-source에 원본 XYZ를 지정합니다.

ligand_spatial CTest는 연결 구조, 평면→입체 대응, 강체 회전의 거리·역변환, 시점에 따른 회색 갱신, native/CDXML 저장, 복사·배율, 추가 CO 보존, 실제 편집 창의 미리보기·취소·한 단계 실행 취소를 검사합니다. 모든 로컬 UI 테스트와 CTest는 scripts/test-isolated.ps1을 통해 격리 데스크톱에서 실행합니다.
