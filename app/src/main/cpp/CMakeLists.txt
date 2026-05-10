cmake_minimum_required(VERSION 3.22.1)
project("guitarfx")

add_library(guitarfx SHARED
        engine.cpp
        effects.cpp
        core/tuner.cpp
        core/looper.cpp
        hal/audio_hal_android.cpp
        hal/sensor_hal_android.cpp
        hal/file_hal_android.cpp
        DaisySP/Effects/overdrive.cpp
        DaisySP/Effects/chorus.cpp
        DaisySP/Effects/flanger.cpp
        DaisySP/Effects/phaser.cpp
        DaisySP/Effects/tremolo.cpp
        DaisySP/Effects/autowah.cpp
        DaisySP/Effects/wavefolder.cpp
        DaisySP/Filters/ladder.cpp
        DaisySP/Filters/svf.cpp
        DaisySP/Dynamics/limiter.cpp
        DaisySP/Synthesis/oscillator.cpp
)

target_include_directories(guitarfx PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/core
        ${CMAKE_CURRENT_SOURCE_DIR}/hal
        ${CMAKE_CURRENT_SOURCE_DIR}/DaisySP
        ${CMAKE_CURRENT_SOURCE_DIR}/DaisySP/Utility
)

target_link_libraries(guitarfx
        android
        log
        aaudio
        m
)
