QT += core testlib
QT -= gui

CONFIG += c++17 testcase console
CONFIG -= app_bundle

TARGET = audio_parser_test

INCLUDEPATH += ../../src

SOURCES += \
    audio_parser_test.cpp \
    ../../src/audio_parser.cpp

HEADERS += \
    ../../src/audio_parser.h
