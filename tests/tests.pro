TEMPLATE = subdirs
CONFIG += ordered

SUBDIRS += \
    audio_parser_test \
    icons_test

# `make check` ile tüm test binary'lerini sırayla çalıştır.
# Her alt-proje kendi `check` target'ını expose eder (CONFIG += testcase).
