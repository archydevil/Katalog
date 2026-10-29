# Teca control style (fork, SpecMediaIntelligence.md D34): the native Qt Quick
# controls (fields, lists, check boxes, buttons, menus) drawn in the "Control
# Center" design system. Each control derives from its Fusion counterpart and
# changes only how it looks; every control not listed here is Fusion's own.
# Colours come from the palette, which Main.qml sets from the KTheme tokens.
qt_add_library(tecastyle STATIC)
qt_add_qml_module(tecastyle
    URI TecaStyle
    VERSION 1.0
    IMPORTS QtQuick.Controls.Fusion
    QML_FILES
        Button.qml
        CheckBox.qml
        ComboBox.qml
        Menu.qml
        MenuItem.qml
        RadioButton.qml
        SpinBox.qml
        Switch.qml
        TextArea.qml
        TextField.qml
        ToolButton.qml
)
target_link_libraries(tecastyle PRIVATE Qt6::Quick Qt6::QuickControls2)
