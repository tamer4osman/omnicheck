# Sanitizer harness (plan risk R2 mitigation: every CI run proves the C core
# is leak-, UB- and double-free-clean under ASan/UBSan).
if(OC_SANITIZE_ADDRESS)
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address)
endif()
if(OC_SANITIZE_UNDEFINED)
    add_compile_options(-fsanitize=undefined)
    add_link_options(-fsanitize=undefined)
endif()
