add_library(spandsp_g722 STATIC
    src/g722.c
    src/alloc.c
    src/vector_int.c
)
set_source_files_properties(
    src/g722.c
    src/alloc.c
    src/vector_int.c
    PROPERTIES LANGUAGE C
)
target_include_directories(spandsp_g722 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_definitions(spandsp_g722 PRIVATE
    HAVE_STDBOOL_H=1
    HAVE_MATH_H=1
    # Linux/glibc já expõe sinf/cosf/… via math.h; evita shims estáticos em floating_fudge.h
    HAVE_SINF=1
    HAVE_COSF=1
    HAVE_TANF=1
    HAVE_ASINF=1
    HAVE_ACOSF=1
    HAVE_ATANF=1
    HAVE_ATAN2F=1
    HAVE_CEILF=1
    HAVE_FLOORF=1
    HAVE_POWF=1
    HAVE_EXPF=1
    HAVE_LOGF=1
    HAVE_LOG10F=1
)
