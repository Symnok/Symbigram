# Desktop harness for the protocol core (see main.cpp). Build with the Desktop Qt 4.7.4
# MinGW kit from the Qt SDK: qmake && mingw32-make.
TEMPLATE = app
TARGET = symbigram-cli
CONFIG += console
CONFIG -= app_bundle
QT += gui

include(../../core.pri)

SOURCES += main.cpp
