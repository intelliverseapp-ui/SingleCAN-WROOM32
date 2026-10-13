idf_component_register(
    SRCS
        "test_singlecan_responses.c"
        "../../../main/singlecan_responses.c"

    INCLUDE_DIRS
        "."
        "../../../main"

    REQUIRES
        unity
        json
)
