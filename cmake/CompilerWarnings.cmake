# Strict-but-sane warning profile for first-party OmniCheck targets.
# Third-party vendored code (tree-sitter) is compiled in separate targets
# that never receive these flags, and its headers are marked SYSTEM.
function(oc_set_warnings target)
    set(GNU_LIKE "$<C_COMPILER_ID:GNU,Clang,AppleClang>")
    target_compile_options(${target} PRIVATE
        "$<${GNU_LIKE}:-Wall;-Wextra;-Wpedantic;-Wshadow;-Wstrict-prototypes;-Wmissing-prototypes;-Wundef>"
        "$<$<C_COMPILER_ID:MSVC>:/W4>"
    )
    if(OC_WERROR)
        target_compile_options(${target} PRIVATE
            "$<${GNU_LIKE}:-Werror>"
            "$<$<C_COMPILER_ID:MSVC>:/WX>"
        )
    endif()
endfunction()
