# ESP32-S3 CSI TX/RX 역할 교대 펌웨어

이 저장소는 향후 **TX/RX 역할이 교대하는 2TX·2RX** 실험에 사용할 펌웨어입니다. 같은 바이너리에서 진단용 `1X3` 모드도 사용할 수 있습니다. ESP-IDF **6.0.1**, ESP32-S3 기준이며, 펌웨어 소스, 검증된 이미지, 설치 스크립트와 펌웨어 테스트만 포함합니다. 수집기 구현은 포함하지 않습니다.

현재 진행하는 고정 TX/RX의 1TX·3RX 및 1TX·4RX 실험은 **기존의 별도 고정 TX/RX 펌웨어**를 사용합니다. 이 저장소에는 별도 `1X4` 모드나 E 노드 지원을 추가하지 않았습니다. 현재 고정 역할 실험을 위해 이 교대 펌웨어로 교체할 필요는 없습니다.

| 모드 | 필요한 보드 | 센싱 TX | 센싱 RX |
| --- | --- | --- | --- |
| `1X3` | A–D, 4개 | A | B, C, D |
| `2X2` | A–D, 4개 | A/B ↔ C/D | C/D ↔ A/B |

기본 목표는 **활성 TX당 40 Hz**입니다. `2X2`의 두 TX는 슬롯을 번갈아 사용하며, 기본 400슬롯마다 역할 그룹이 교대합니다(명목 5초). 전파 간섭과 송신 지연으로 실제 주파수가 낮아지거나 교대 시간이 길어질 수 있습니다.

## 빌드 없이 설치하기

모든 보드에 같은 `prebuilt/` 이미지를 사용합니다. **Python과 esptool 5.x만 있으면 설치할 수 있으며, ESP-IDF 전체 빌드는 필요하지 않습니다.** ESP-IDF 6.0.1에 포함된 Python/esptool도 사용할 수 있습니다.

저장소 루트의 PowerShell에서, 포트 이름을 실제 연결된 보드에 맞춰 실행합니다. 다른 시리얼 모니터는 먼저 닫습니다.

```powershell
# Python 환경에 esptool이 없다면 최초 한 번만 설치
python -m pip install 'esptool>=5,<6'

# 재빌드 없이 체크섬 검사 후 설치
.\scripts\flash.ps1 -Port COM3

# ESP-IDF에 포함된 Python을 직접 지정하는 예시
.\scripts\flash.ps1 -Port COM3 -Python 'C:\Espressif\python_env\idf6.0_py3.12_env\Scripts\python.exe'

# 장치 연결 없이 이미지/명령만 검사
.\scripts\flash.ps1 -Port COM3 -DryRun
```

기본 플래시 속도는 **460800 baud**입니다. 통신 오류가 있을 때만 `-Baud 115200`으로 재시도합니다. 실행 정책이 스크립트를 막으면, `prebuilt/`에서 아래 명령으로 같은 이미지를 설치할 수 있습니다.

```powershell
cd prebuilt
python -m esptool --chip esp32s3 -p COM3 -b 460800 write-flash "@flash_args"
```

이 명령은 부트로더 `0x0`, 파티션 테이블 `0xa000`, 앱 `0x20000`을 기록합니다. 이미지의 플래시 설정은 DIO/80 MHz/2 MB이고, 검증했던 보드는 물리 플래시가 8 MB인 ESP32-S3입니다. 보드의 메모리 전체를 지우지는 않습니다. 기존 팀 펌웨어와 같은 레이아웃이면 NVS(`0xb000`, 길이 `0x6000`)를 보존합니다. 다른 레이아웃/용도로 쓰던 보드는 기존 데이터를 백업한 뒤 설치하세요.

## 이 펌웨어의 진단용 1X3 모드

설치 후 콘솔은 **921600 baud, 8N1**입니다. 아래 명령은 줄바꿈(LF)으로 끝내며, 각 행을 해당 보드에만 전송합니다. 보드마다 ID가 달라야 합니다.

```text
NODE CONFIG A 1X3 40 5000
NODE CONFIG B 1X3 40 5000
NODE CONFIG C 1X3 40 5000
NODE CONFIG D 1X3 40 5000
```

각 보드에서 `NODE STATUS`로 ID/모드/40 Hz를 확인합니다. B/C/D에 `NODE START 1001`을 먼저 보내고, **A에 마지막으로 같은 명령**을 보내면 시작합니다. 새 실험마다 새로운 공통 session 번호(1–4294967295)를 사용합니다. `NODE STOP`은 명령을 받은 보드만 정지시키므로, A부터 정지한 뒤 나머지 보드도 정지합니다.

전원을 다시 켜면 저장된 설정을 유지하면서 **항상 STOPPED로 부팅**합니다. 플래시 설치만으로 실험이 자동 시작되지 않습니다.

## 역할이 교대하는 2X2 설정

향후 교대 실험에서는 A–D 네 보드에 같은 이미지를 설치하고 모드를 `2X2`로 설정합니다. B/C/D부터 동일한 session으로 START한 뒤 A를 START합니다. A/B가 센싱 TX이고 C/D가 RX인 구간과, C/D가 TX이고 A/B가 RX인 구간을 번갈아 실행합니다. 역할 교대는 재플래시 없이 같은 펌웨어 안에서 진행합니다.

```text
NODE CONFIG A 2X2 40 5000
NODE CONFIG B 2X2 40 5000
NODE CONFIG C 2X2 40 5000
NODE CONFIG D 2X2 40 5000
```

## 수집기 담당자에게 전달할 규약

[펌웨어 프로토콜 문서](csi_node/README.md)에 UART 명령/응답, 상태와 타이밍 카운터, CSI CSV v2 형식을 정의했습니다. 기존 단일 TX 수집기와 출력 형식이 다릅니다. `CSI_DATA_V2`의 `session, phase, slot, tx_id, rx_id`를 사용해 링크를 구별하고, `csi_b64`를 signed int8 원본 바이트로 복원해야 합니다. `seq`는 전역 슬롯이므로 `2X2`에서 TX별로 2씩 증가합니다.

## 소스 수정 후 빌드

ESP-IDF **6.0.1** PowerShell 환경에서 저장소 루트 기준:

```powershell
.\scripts\build.ps1
```

스크립트는 `%TEMP%\ige-csi-node-idf601`의 영문 경로에 소스를 복사하고 빌드 캐시를 재사용합니다. 최초에만 target을 지정하며, 성공하면 `prebuilt/`의 이미지와 체크섬/소스 해시를 갱신합니다. 임시 경로에 한글이 포함되면 `-StageDirectory C:\temp\ige-csi-node`처럼 지정합니다. 이 디렉터리는 이 프로젝트 전용으로 사용하세요. 설정 변경 시 기존 `sdkconfig` 값이 우선하므로 새 staging 디렉터리를 지정해 빌드하세요.

일반 ESP-IDF CLI도 지원합니다. 영문 경로에서 최초 한 번 `idf.py set-target esp32s3`, 이후 `idf.py build`를 실행합니다. 동일 버전을 여러 보드에 설치할 때는 빌드를 한 번만 하고 `flash.ps1`을 포트별로 실행하면 됩니다.

```powershell
python -m pytest tests/test_node_protocol.py -q
```

위 테스트는 PC용 C 컴파일러(GCC/Clang/MSVC)로 실제 프로토콜 헤더를 컴파일하여 실행합니다. 필요하면 `CSI_HOST_CC`에 컴파일러 경로를 지정합니다.

## 검증 범위

- 이 저장소는 2026-10-14 A/C 두 보드로 검증한 `1X3`/`2X2` 소스와 바이너리를 사용합니다. 단방향 수신, 양방향 역할 교대, 정지 및 재부팅 정지 상태를 실측했으며, 최종 테스트에서 잘못된 CSI 행/CSI 큐 넘침/송신 오류/FAULT는 0이었습니다.
- **전체 4대의 2X2 실험과 장시간 검증은 아직 수행하지 않았습니다.** 두 보드 검증에서는 나머지 B/D가 비어 있었습니다.
- 두 보드 2X2 실측의 스케줄 주파수는 활성 TX당 약 39.05–39.09 Hz, 수신 기록 주파수는 35.75–37.09 Hz였습니다. 40 Hz 설정이 무손실 수신을 보장하지는 않습니다.

기존 TX가 60 Hz였던 원인은 예전 펌웨어의 NVS `csi_ctrl/rate_hz=60`이 소스 기본값 40을 덮어쓴 것이었습니다. 이 펌웨어는 별도 `csi_node_v2` 설정을 쓰므로 이전 60 Hz 값을 가져오지 않습니다.
