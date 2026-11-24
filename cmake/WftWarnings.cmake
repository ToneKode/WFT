# ===========================================================================
# wft_set_warnings(<target>)
#
# One place that decides what "clean" means, so a warning that shows up in a
# fresh build is a real regression rather than a difference between targets.
# ===========================================================================

function(wft_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
        if(WFT_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
        if(WFT_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()
