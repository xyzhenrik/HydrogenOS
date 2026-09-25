qt_add_library(hydrogen-design STATIC)

set_source_files_properties(Tokens.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(hydrogen-design
    URI Hydrogen.Design
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/Hydrogen/Design"
    QML_FILES
        Tokens.qml
        GlassSurface.qml
        HydrogenButton.qml
        SectionLabel.qml
)

qt_add_shaders(hydrogen-design "hydrogen-design-shaders"
    PREFIX /qt/qml/Hydrogen/Design
    FILES shaders/glass-highlight.frag
)

target_link_libraries(hydrogen-design PRIVATE Qt6::Quick)
