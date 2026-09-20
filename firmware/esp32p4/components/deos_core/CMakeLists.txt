idf_component_register(
    SRCS
        "../../../../core/src/resource.cpp"
        "../../../../core/src/plan.cpp"
        "../../../../core/src/reconciler.cpp"
        "../../../../core/src/event_bus.cpp"
    INCLUDE_DIRS
        "../../../../core/include"
)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
