# Part of Beast

GroupSources(extras/beast beast)
GroupSources(include/beast beast)
GroupSources(test/core "/")

add_executable (detail-tests
    ${BEAST_INCLUDES}
    ../../extras/beast/unit_test/main.cpp
    zlib.cpp
)

if (NOT WIN32)
    target_link_libraries(detail-tests ${Boost_LIBRARIES} Threads::Threads)
endif()
