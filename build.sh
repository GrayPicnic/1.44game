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
    -lgdi32 -luser32 -lkernel32 -lm

echo "빌드 완료: $OUT"
ls -lh "$OUT"
