# 1.44game

2P GAME ARCADE 「1.44MB GAME_DEV CONTEST」 출품작 프로젝트.
전체 배포 패키지(실행파일+에셋)를 1,474,560 bytes(1.44MB) 이하로 유지하는 것이 목표.

- 마감: 2026-09-04 23:59 (KST)
- 규칙: 인터넷/서버 없이 독립 실행되는 실행파일이어야 함 (웹 게임 불가)

## 구조

- `src/main.c` — 순수 Win32 API + GDI 소프트웨어 렌더링 기반 게임 골격 (외부 런타임 의존성 없음)
- `assets/` — 게임 리소스 (이미지, 사운드 등)
- `build.sh` — mingw-w64로 Windows용 실행파일을 크기 최적화 빌드
- `check_size.sh` — 빌드 결과물이 1.44MB 제한 내에 있는지 검사

## 빌드 방법

```sh
./build.sh          # dist/game.exe 생성
./check_size.sh      # 용량 확인
```

Windows에서 실행하려면 `dist/game.exe`를 그대로 복사해서 더블클릭.
Linux 환경에서는 Wine으로 테스트 가능: `wine dist/game.exe`

## 조작

- 방향키 또는 WASD: 이동
- ESC: 종료
