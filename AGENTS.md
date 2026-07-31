# nutcracker-idf 에이전트 가이드

## 언어
- 이 프로젝트의 **문서**(README, 설계/계획 스펙, Agent 메모 등)와 **사용자 답변**은 **한글**로 작성한다.
- 코드 식별자, 파일명, 프로토콜/라이브러리 고유명, 로그 태그 등은 기존 ESP-IDF / panzer4 관례(영문)를 따른다.
- 커밋 메시지는 한글 또는 영문 모두 가능하나, 변경 요지를 분명히 쓴다.

## 기술 스택
- Framework: ESP-IDF v5.5.x
- 언어: C
- 빌드: CMake / idf.py. 루트 `env.bat`로 IDF 환경을 먼저 잡는다.
- 타깃: ESP32 classic
- 입력: Bluepad32 + BTstack

## 참고
- 설계: `docs/superpowers/specs/2026-07-31-nutcracker-idf-design.md`
- 기반 프로젝트: `../panzer4-idf`
