cmake_minimum_required(VERSION 3.7)
project(Filters)

include(filters.cmake)

add_definitions(-fpic)

SET(LIBALAC_SRC
        ${CMAKE_CURRENT_SOURCE_DIR}/dec_alac.c
        ${CMAKE_CURRENT_SOURCE_DIR}/alac_decode.cpp
)

# Decoder half of Apple's reference implementation only: ALACDecoder plus the
# ag/dp/matrix decode helpers. The encoder and the convert-utility, which needs
# CoreAudio, are left out.
SET(LIBALAC_LIBS
        ${CMAKE_CURRENT_SOURCE_DIR}/lib/libalac.a
)

SET(LIBALAC_INC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_filter(libalac
        "${LIBALAC_SRC}"
        "${LIBALAC_LIBS}"
        []
        ""
        "${LIBALAC_INC}"
        ""
        "1")
