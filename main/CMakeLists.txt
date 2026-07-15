idf_component_register(
    SRC_DIRS "." "core" "hardware" "ml"
    INCLUDE_DIRS "." "core" "hardware" "ml"
)

# Force compile
set(COMPONENT_SRCS "${COMPONENT_SRCS}" "core/bmi270_context.c")
