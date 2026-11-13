function(cosmic_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8 /wd4100 /wd4244 /wd4267 /wd4305)
        target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wno-unused-parameter
            -Wno-missing-field-initializers)
    endif()
    cosmic_set_deterministic_fp(${target})
endfunction()

# Deterministic floating point: the simulation must give bit-identical results with MSVC,
# GCC and Clang (replays re-simulate a run from its inputs). No fast-math, no fused
# multiply-add contraction, strict IEEE semantics; transcendental functions come from
# engine/core/DetMath.h, not from the C library.
function(cosmic_set_deterministic_fp target)
    if(MSVC)
        # Since Visual Studio 2022 (17.0), /fp:precise no longer contracts a*b+c into FMA.
        target_compile_options(${target} PRIVATE /fp:precise)
    else()
        target_compile_options(${target} PRIVATE -ffp-contract=off -fno-fast-math)
    endif()
endfunction()
