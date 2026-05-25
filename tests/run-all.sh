#!/bin/bash
# Tüm test binary'lerini sırayla çalıştır. PASS/FAIL summary verir.
# Headless ortamda: QT_QPA_PLATFORM=offscreen ./tests/run-all.sh
set -u
cd "$(dirname "$0")"

# Build (eğer yapılmadıysa)
if [ ! -f audio_parser_test/audio_parser_test ] || [ ! -f icons_test/icons_test ]; then
    echo "==> Building tests..."
    qmake6 tests.pro >/dev/null
    make -j$(nproc) >/dev/null
fi

# Offscreen Qt platform — headless CI / SSH için
export QT_QPA_PLATFORM=${QT_QPA_PLATFORM:-offscreen}

passed=0
failed=0
for t in audio_parser_test/audio_parser_test icons_test/icons_test; do
    name=$(basename "$t")
    echo ""
    echo "==> $name"
    if ./$t; then
        ((passed++))
    else
        ((failed++))
        echo "FAIL: $name"
    fi
done

echo ""
echo "===================="
echo "Total: $((passed + failed))  Passed: $passed  Failed: $failed"
[ $failed -eq 0 ]
