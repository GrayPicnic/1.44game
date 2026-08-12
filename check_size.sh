#!/usr/bin/env bash
# 배포 패키지(dist/) 전체 용량이 1.44MB(1,474,560 bytes) 이하인지 확인한다.
set -euo pipefail

LIMIT=1474560  # 3.5" 플로피 디스크 실제 용량 (bytes)
DIST_DIR="dist"
ZIP_PATH="dist_package.zip"

if [ ! -d "$DIST_DIR" ]; then
    echo "dist/ 폴더가 없습니다. 먼저 ./build.sh 를 실행하세요."
    exit 1
fi

rm -f "$ZIP_PATH"
( cd "$DIST_DIR" && zip -r -q "../$ZIP_PATH" . )

RAW_SIZE=$(du -sb "$DIST_DIR" | cut -f1)
ZIP_SIZE=$(stat -c%s "$ZIP_PATH")

echo "dist/ 원본 합계 : $RAW_SIZE bytes"
echo "zip 압축 후     : $ZIP_SIZE bytes"
echo "제한(1.44MB)    : $LIMIT bytes"
echo

if [ "$RAW_SIZE" -le "$LIMIT" ]; then
    echo "✅ 원본 합계 기준 통과 ($((LIMIT - RAW_SIZE)) bytes 여유)"
else
    echo "❌ 원본 합계가 제한을 $((RAW_SIZE - LIMIT)) bytes 초과합니다."
fi
