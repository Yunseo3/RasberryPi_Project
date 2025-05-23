라즈베리파이를 이용한 IoT 장치 제어 시스템입니다. 웹 인터페이스와 TCP 소켓을 통해 LED, 7세그먼트, 부저, 조도센서를 제어할 수 있습니다.

## 프로젝트 개요
라즈베리파이 기반의 IoT 장치 제어 시스템으로, 다음과 같은 목표를 가지고 개발되었습니다:
- **모듈화된 아키텍처**: 각 장치별 독립적인 라이브러리 구조
- **다중 인터페이스**: 웹 UI와 TCP 소켓 동시 지원
- **실시간 제어**: 멀티스레드 기반 비동기 처리
- **확장성**: 동적 라이브러리를 통한 기능 확장 가능

## 개발 일정
### 1일차: 회로 구성 및 기본 기능 개발
- 하드웨어 회로 구성 (7SEGMENT, LED, CDS)
- GPIO 핀 설정 및 테스트
- 기본 장치 제어 라이브러리 개발
- wiringPi 기반 하드웨어 제어 구현

### 2일차: 고급 기능 및 통신 개발
- 7세그먼트 카운트다운 기능 구현
- 조도센서 자동 제어 로직 개발
- TCP 서버-클라이언트 통신 구현
- 명령어 처리 시스템 구축

### 3일차: 웹 서버 및 최종 완성
- HTTP 웹 서버 기능 추가
- 웹 UI 인터페이스 개발
- 통합 종료 기능 (ALL_OFF) 구현
- 전체 시스템 디버깅 및 최적화



## 프로젝트 구조
![image](https://github.com/user-attachments/assets/491006a8-2199-48eb-8311-4b9bf8660df3)

## 시스템 요구사항
- 라즈베리파이 4 (또는 3)
- Raspbian OS
- WiringPi 라이브러리
- 하드웨어: LED, 7세그먼트, 부저, 조도센서

## 주요 기능

### LED (GPIO 18)
- ON/OFF 제어
- PWM 기반 3단계 밝기 조절 (10%, 50%, 100%)

### 7-SEGMENT (GPIO 16, 20, 21, 12)
- 0~9 숫자 표시
- 카운트다운 기능 (1초마다 -1 감소)
- 0 도달 시 자동 부저 작동

### BUZZER (GPIO 19)
- 학교종 멜로디 재생
- ON/OFF 제어

### 조도 센서
- 실시간 조도 측정
- 자동 LED(GPIO 17) 제어
- 어두우면 자동 점등, 밝으면 자동 소등

## 하드웨어 연결
```
LED (PWM)        : GPIO 18
자동 LED         : GPIO 17
BUZZER          : GPIO 19
7-SEGMENT       : GPIO 16, 20, 21, 12
조도센서 (I2C)   : SDA(GPIO 2), SCL(GPIO 3)
```

## 주요 명령어
- `LED_ON` / `LED_OFF`: LED 켜기/끄기
- `LED_BRIGHTNESS 0~2`: LED 밝기 조절
- `SEGMENT_DISPLAY 0~9`: 7세그먼트에 숫자 표시
- `SEGMENT_COUNTDOWN 1~9`: 카운트다운 시작
- `SEGMENT_STOP`: 카운트다운 중지
- `BUZZER_PLAY` / `BUZZER_STOP`: 부저 재생/중지
- `CDS_READ`: 조도값 읽기
- `CDS_AUTO_START` / `CDS_AUTO_STOP`: 자동 LED 제어 시작/중지
- `ALL_OFF`: 모든 장치 끄기
- `HELP`: 도움말 보기

## 실행 방법
```bash
make
sudo ./iot_server -d          # 데몬모드
telnet localhost 8080         # 클라이언트 실행
http://라즈베리ip주소:8080     # 웹서버 실행
```

## 기능 상세

### 1. LED
![image](https://github.com/user-attachments/assets/ab8c0fc5-9014-4cb9-aa3f-8691cccae1bd)

#### LED 제어 예시
![LED_ON](https://github.com/user-attachments/assets/3ad9bbea-3597-41f4-9b87-86d8bbeacbd5)
- `LED_ON`: LED가 켜짐

![LED_OFF](https://github.com/user-attachments/assets/78a5812e-1aef-4fee-b428-e70b8317d4ec)
- `LED_OFF`: LED가 꺼짐

![LED_BRIGHTNESS 0](https://github.com/user-attachments/assets/d472eb54-0603-419a-ba86-bea78d3052d3)
- `LED_BRIGHTNESS 0`: 최저 밝기

![LED_BRIGHTNESS 1](https://github.com/user-attachments/assets/567e5043-5530-4d72-ac69-9b3a0885f080)
- `LED_BRIGHTNESS 1`: 중간 밝기

![LED_BRIGHTNESS 2](https://github.com/user-attachments/assets/68f83e88-83da-4bb3-9c64-a21591c2cd4b)
- `LED_BRIGHTNESS 2`: 최대 밝기

### 2. SEGMENT
![image](https://github.com/user-attachments/assets/d60f360a-8889-4a9b-b2a8-73be2e59021f)

![심화실습평가_남윤서](https://github.com/user-attachments/assets/f3f0a60d-e665-4702-a99b-abd40f68c096)

- `SEGMENT_DISPLAY`: 입력한 숫자 출력

![심화실습평가_남윤서(1)](https://github.com/user-attachments/assets/e5bbb476-08ce-4960-af66-b36a327fa784)

- `SEGMENT_COUNTDOWN`: 카운트 다운 시작, 0이 되면 자동으로 부저가 울림
- `SEGMENT_STOP`: 카운트다운 중지

### 3. BUZZER
![image](https://github.com/user-attachments/assets/4bb7588a-d6cc-42f4-b2fa-2ea0099a9058)

![심화실습평가_남윤서(11)](https://github.com/user-attachments/assets/d92a0f25-b826-46b5-bb37-7934dd4506f3)

- `BUZZER_PLAY`: 음악 출력

![심화실습평가_남윤서(11)](https://github.com/user-attachments/assets/e9d64a54-31c1-4acf-b75b-afa6078efbbf)

- `BUZZER_STOP`: 음악 종료

### 4. CDS (조도센서)
![image](https://github.com/user-attachments/assets/f04f1457-edf5-4c32-9178-35216af688bf)

![심화실습평가_남윤서(4)](https://github.com/user-attachments/assets/144a785a-9cff-4298-bbe5-5daa44627c0d)
- `CDS_READ`: 값 읽기

![심화실습평가_남윤서(5)](https://github.com/user-attachments/assets/e9edfd60-4828-4bc6-bbd7-70be29ce50a5)
![심화실습평가_남윤서(4)](https://github.com/user-attachments/assets/144a785a-9cff-4298-bbe5-5daa44627c0d)

- `CDS_AUTO_START`: 밝지 않으면 LED가 켜집니다. 밝으면 LED가 꺼집니다.
- `CDS_AUTO_STOP`: 끄기

## 추가 기능

### 1. 웹서버
![image](https://github.com/user-attachments/assets/11edc116-27ce-4fb2-a73d-f80e5d524653)
아래에 각 기능에 대한 설명

### 2. 모든 기능 종료
![image](https://github.com/user-attachments/assets/49247ff1-aae0-4a3f-a6a5-7c188ada9e89)
- `ALL_OFF`: 모든 기능 꺼짐



## 성능 최적화
- 공유 라이브러리로 메모리 효율성 증대
- 멀티스레드로 동시성 향상
- 데몬 프로세스로 시스템 리소스 최적화

## 추가 기능

### 1. 웹서버
![image](https://github.com/user-attachments/assets/11edc116-27ce-4fb2-a73d-f80e5d524653)
아래에 각 기능에 대한 설명

### 2. 모든 기능 종료
![심화실습평가_남윤서(14)](https://github.com/user-attachments/assets/142d14a3-f3f9-4709-a3ae-ae9e6020b593)
![심화실습평가_남윤서(16)](https://github.com/user-attachments/assets/27c7c003-e8b7-498d-ac5d-dfaf75bf8e7a)

- `ALL_OFF`: 모든 기능 꺼짐

## 확장 가능성

### 추가 가능한 기능
- **센서 확장**: 온습도, 초음파, PIR 센서 추가
- **데이터베이스**: 센서 데이터 로깅 및 분석

