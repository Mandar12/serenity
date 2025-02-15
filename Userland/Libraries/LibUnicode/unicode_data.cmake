option(ENABLE_UNICODE_DATABASE_DOWNLOAD "Enable download of Unicode UCD files at build time" ON)

set(UNICODE_DATA_URL https://www.unicode.org/Public/13.0.0/ucd/UnicodeData.txt)
set(UNICODE_DATA_PATH ${CMAKE_BINARY_DIR}/UCD/UnicodeData.txt)

set(SPECIAL_CASING_URL https://www.unicode.org/Public/13.0.0/ucd/SpecialCasing.txt)
set(SPECIAL_CASING_PATH ${CMAKE_BINARY_DIR}/UCD/SpecialCasing.txt)

set(PROP_LIST_URL https://www.unicode.org/Public/13.0.0/ucd/PropList.txt)
set(PROP_LIST_PATH ${CMAKE_BINARY_DIR}/UCD/PropList.txt)

set(DERIVED_CORE_PROP_URL https://www.unicode.org/Public/13.0.0/ucd/DerivedCoreProperties.txt)
set(DERIVED_CORE_PROP_PATH ${CMAKE_BINARY_DIR}/UCD/DerivedCoreProperties.txt)

set(DERIVED_BINARY_PROP_URL https://www.unicode.org/Public/13.0.0/ucd/extracted/DerivedBinaryProperties.txt)
set(DERIVED_BINARY_PROP_PATH ${CMAKE_BINARY_DIR}/UCD/DerivedBinaryProperties.txt)

set(PROP_ALIAS_URL https://www.unicode.org/Public/13.0.0/ucd/PropertyAliases.txt)
set(PROP_ALIAS_PATH ${CMAKE_BINARY_DIR}/UCD/PropertyAliases.txt)

set(PROP_VALUE_ALIAS_URL https://www.unicode.org/Public/13.0.0/ucd/PropertyValueAliases.txt)
set(PROP_VALUE_ALIAS_PATH ${CMAKE_BINARY_DIR}/UCD/PropertyValueAliases.txt)

set(SCRIPTS_URL https://www.unicode.org/Public/13.0.0/ucd/Scripts.txt)
set(SCRIPTS_PATH ${CMAKE_BINARY_DIR}/UCD/Scripts.txt)

set(SCRIPT_EXTENSIONS_URL https://www.unicode.org/Public/13.0.0/ucd/ScriptExtensions.txt)
set(SCRIPT_EXTENSIONS_PATH ${CMAKE_BINARY_DIR}/UCD/ScriptExtensions.txt)

set(WORD_BREAK_URL https://www.unicode.org/Public/13.0.0/ucd/auxiliary/WordBreakProperty.txt)
set(WORD_BREAK_PATH ${CMAKE_BINARY_DIR}/UCD/WordBreakProperty.txt)

set(EMOJI_DATA_URL https://www.unicode.org/Public/13.0.0/ucd/emoji/emoji-data.txt)
set(EMOJI_DATA_PATH ${CMAKE_BINARY_DIR}/UCD/emoji-data.txt)

if (ENABLE_UNICODE_DATABASE_DOWNLOAD)
    if (NOT EXISTS ${UNICODE_DATA_PATH})
        message(STATUS "Downloading UCD UnicodeData.txt from ${UNICODE_DATA_URL}...")
        file(DOWNLOAD ${UNICODE_DATA_URL} ${UNICODE_DATA_PATH} INACTIVITY_TIMEOUT 10)
    endif()
    if (NOT EXISTS ${SPECIAL_CASING_PATH})
        message(STATUS "Downloading UCD SpecialCasing.txt from ${SPECIAL_CASING_URL}...")
        file(DOWNLOAD ${SPECIAL_CASING_URL} ${SPECIAL_CASING_PATH} INACTIVITY_TIMEOUT 10)
    endif()
    if (NOT EXISTS ${PROP_LIST_PATH})
        message(STATUS "Downloading UCD PropList.txt from ${PROP_LIST_URL}...")
        file(DOWNLOAD ${PROP_LIST_URL} ${PROP_LIST_PATH} INACTIVITY_TIMEOUT 10)
    endif()
    if (NOT EXISTS ${DERIVED_CORE_PROP_PATH})
        message(STATUS "Downloading UCD DerivedCoreProperties.txt from ${DERIVED_CORE_PROP_URL}...")
        file(DOWNLOAD ${DERIVED_CORE_PROP_URL} ${DERIVED_CORE_PROP_PATH} INACTIVITY_TIMEOUT 10)
    endif()
    if (NOT EXISTS ${DERIVED_BINARY_PROP_PATH})
        message(STATUS "Downloading UCD DerivedBinaryProperties.txt from ${DERIVED_BINARY_PROP_URL}...")
        file(DOWNLOAD ${DERIVED_BINARY_PROP_URL} ${DERIVED_BINARY_PROP_PATH} INACTIVITY_TIMEOUT 10)
    endif()
    if (NOT EXISTS ${PROP_ALIAS_PATH})
        message(STATUS "Downloading UCD PropertyAliases.txt from ${PROP_ALIAS_URL}...")
        file(DOWNLOAD ${PROP_ALIAS_URL} ${PROP_ALIAS_PATH} INACTIVITY_TIMEOUT 10)
    endif()
    if (NOT EXISTS ${PROP_VALUE_ALIAS_PATH})
        message(STATUS "Downloading UCD PropertyValueAliases.txt from ${PROP_VALUE_ALIAS_URL}...")
        file(DOWNLOAD ${PROP_VALUE_ALIAS_URL} ${PROP_VALUE_ALIAS_PATH} INACTIVITY_TIMEOUT 10)
    endif()
    if (NOT EXISTS ${SCRIPTS_PATH})
        message(STATUS "Downloading UCD Scripts.txt from ${SCRIPTS_URL}...")
        file(DOWNLOAD ${SCRIPTS_URL} ${SCRIPTS_PATH} INACTIVITY_TIMEOUT 10)
    endif()
    if (NOT EXISTS ${SCRIPT_EXTENSIONS_PATH})
        message(STATUS "Downloading UCD ScriptExtensions.txt from ${SCRIPT_EXTENSIONS_URL}...")
        file(DOWNLOAD ${SCRIPT_EXTENSIONS_URL} ${SCRIPT_EXTENSIONS_PATH} INACTIVITY_TIMEOUT 10)
    endif()
    if (NOT EXISTS ${WORD_BREAK_PATH})
        message(STATUS "Downloading UCD WordBreakProperty.txt from ${WORD_BREAK_URL}...")
        file(DOWNLOAD ${WORD_BREAK_URL} ${WORD_BREAK_PATH} INACTIVITY_TIMEOUT 10)
    endif()
    if (NOT EXISTS ${EMOJI_DATA_PATH})
        message(STATUS "Downloading UCD emoji-data.txt from ${EMOJI_DATA_URL}...")
        file(DOWNLOAD ${EMOJI_DATA_URL} ${EMOJI_DATA_PATH} INACTIVITY_TIMEOUT 10)
    endif()

    set(UNICODE_DATA_HEADER LibUnicode/UnicodeData.h)
    set(UNICODE_DATA_IMPLEMENTATION LibUnicode/UnicodeData.cpp)

    if (CMAKE_CURRENT_BINARY_DIR MATCHES ".*/LibUnicode") # Serenity build.
        set(UNICODE_DATA_HEADER UnicodeData.h)
        set(UNICODE_DATA_IMPLEMENTATION UnicodeData.cpp)
    endif()

    add_custom_command(
        OUTPUT ${UNICODE_DATA_HEADER} ${UNICODE_DATA_IMPLEMENTATION}
        COMMAND $<TARGET_FILE:GenerateUnicodeData> -h ${UNICODE_DATA_HEADER} -c ${UNICODE_DATA_IMPLEMENTATION} -u ${UNICODE_DATA_PATH} -s ${SPECIAL_CASING_PATH} -p ${PROP_LIST_PATH} -d ${DERIVED_CORE_PROP_PATH} -b ${DERIVED_BINARY_PROP_PATH} -a ${PROP_ALIAS_PATH} -v ${PROP_VALUE_ALIAS_PATH} -r ${SCRIPTS_PATH} -x ${SCRIPT_EXTENSIONS_PATH} -w ${WORD_BREAK_PATH} -e ${EMOJI_DATA_PATH}
        VERBATIM
        DEPENDS GenerateUnicodeData ${UNICODE_DATA_PATH} ${SPECIAL_CASING_PATH} ${PROP_LIST_PATH} ${DERIVED_CORE_PROP_PATH} ${DERIVED_BINARY_PROP_PATH} ${PROP_ALIAS_PATH} ${PROP_VALUE_ALIAS_PATH} ${SCRIPTS_PATH} ${SCRIPT_EXTENSIONS_PATH} ${WORD_BREAK_PATH} ${EMOJI_DATA_PATH}
    )

    set(UNICODE_DATA_SOURCES ${UNICODE_DATA_HEADER} ${UNICODE_DATA_IMPLEMENTATION})
    add_compile_definitions(ENABLE_UNICODE_DATA=1)
else()
    add_compile_definitions(ENABLE_UNICODE_DATA=0)
endif()
