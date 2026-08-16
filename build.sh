#!/usr/bin/env bash
# 1.44MB 콘테스트용 빌드 스크립트.
# mingw-w64로 Windows용 단일 실행파일을 크기 최적화하여 빌드한다.
set -euo pipefail

CC=x86_64-w64-mingw32-gcc
OUT=dist/game.exe

mkdir -p dist

"$CC" \
    -Os -s \
    -static -static-libgcc \
    -mwindows \
    -o "$OUT" \
    src/main.c \
    -lgdi32 -luser32 -lkernel32 -lm -lwinmm

# 배포 패키지에 아트 에셋도 같이 담는다 (exe 옆 images/ 폴더에서 런타임에 읽음)
if [ -d images ]; then
    mkdir -p dist/images
    cp -f images/*.bmp dist/images/ 2>/dev/null || true
fi

# 무전 콜아웃 보이스 에셋(숫자 0~9 PCM)도 같이 담는다 (exe 옆 audio/ 폴더에서 런타임에 읽음)
if [ -d audio ]; then
    mkdir -p dist/audio
    cp -f audio/*.bin dist/audio/ 2>/dev/null || true
fi

echo "빌드 완료: $OUT"
ls -lh "$OUT"
du -sh dist
