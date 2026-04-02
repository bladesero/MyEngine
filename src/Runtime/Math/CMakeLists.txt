add_library(MyEngineMath INTERFACE
    Vector2.hpp
    Vector3.hpp
    Vector4.hpp
)

target_link_libraries(MyEngineCore PUBLIC MyEngineMath)