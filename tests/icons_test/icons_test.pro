QT += core gui testlib
QT -= widgets

CONFIG += c++17 testcase console
CONFIG -= app_bundle

TARGET = icons_test

INCLUDEPATH += ../../src

SOURCES += \
    icons_test.cpp \
    ../../src/icons.cpp

HEADERS += \
    ../../src/icons.h
