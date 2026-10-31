set(CORE_HEADERS
    collection.h
    collectionaccess.h
    commandline.h
    catalog.h
    database.h
    device.h
    search.h
    searchjob.h
    searchjobstoppable.h
    searchmanager.h
    searchprogressmanager.h
    searchresultsthrottler.h
    storage.h
    tag.h
    language.h
    catalogjob.h
    catalogjobstoppable.h
    catalogmanager.h
    catalogprogressmanager.h
    devicejobstoppable.h
    devicemanager.h
    deviceupdatemanager.h
    filemetadata.h
    filetypemapping.h
    parallelmetadataextractor.h
    statusbarmessagebuilder.h
    backupprofilegenerator.h
    backupmappingmanager.h
    filechecksum.h
    foldertreeloader.h
    databasemanager.h
    catalogdifferenceengine.h
    directoryreplicator.h
    backupjob.h
    backupjobstoppable.h
    collectionimporter.h
    statistics.h
    mdischema.h
    mdiprofile.h
    mdidata.h
    mdienrichmentjob.h
    mdisystemmonitor.h
    mdifts.h
    mdivolumes.h
    mdicontext.h
    mdiknowledge.h
    cataloghistory.h
    mditranscriptfilter.h
    mdipeople.h
    mdifaces.h
    mdiproposals.h
    mdidating.h
    mdiwebsearch.h
    mdidocument.h
    mdihtmlexport.h
    mdidiskspace.h
    mdiarchivecopy.h
)

set(CORE_SOURCES
    collection.cpp
    collectionaccess.cpp
    commandline.cpp
    catalog.cpp
    database.cpp
    device.cpp
    search.cpp
    searchjob.cpp
    searchjobstoppable.cpp
    searchmanager.cpp
    searchprogressmanager.cpp
    searchresultsthrottler.cpp
    storage.cpp
    tag.cpp
    language.cpp
    catalogjob.cpp
    catalogjobstoppable.cpp
    catalogmanager.cpp
    catalogprogressmanager.cpp
    devicejobstoppable.cpp
    devicemanager.cpp
    deviceupdatemanager.cpp
    filemetadata.cpp
    filetypemapping.cpp
    parallelmetadataextractor.cpp
    statusbarmessagebuilder.cpp
    backupprofilegenerator.cpp
    backupmappingmanager.cpp
    filechecksum.cpp
    foldertreeloader.cpp
    databasemanager.cpp
    catalogdifferenceengine.cpp
    directoryreplicator.cpp
    backupjob.cpp
    backupjobstoppable.cpp
    collectionimporter.cpp
    statistics.cpp
    mdischema.cpp
    mdiprofile.cpp
    mdidata.cpp
    mdienrichmentjob.cpp
    mdisystemmonitor.cpp
    mdifts.cpp
    mdivolumes.cpp
    mdicontext.cpp
    mdiknowledge.cpp
    cataloghistory.cpp
    mditranscriptfilter.cpp
    mdipeople.cpp
    mdifaces.cpp
    mdiproposals.cpp
    mdidating.cpp
    mdiwebsearch.cpp
    mdidocument.cpp
    mdihtmlexport.cpp
    mdidiskspace.cpp
    mdiarchivecopy.cpp
)

add_library(katalog-core STATIC ${CORE_SOURCES} ${CORE_HEADERS})

target_link_libraries(katalog-core PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Sql
    Qt6::Network
    KF6::CoreAddons
    KF6::FileMetaData
)

# Fork: archive listing in the enrichment job (zip, tar, 7z) when KArchive is
# available; without it the analysis is skipped (SpecMediaIntelligence.md)
find_package(KF6Archive QUIET)
if (TARGET KF6::Archive)
    target_link_libraries(katalog-core PUBLIC KF6::Archive)
    target_compile_definitions(katalog-core PUBLIC KATALOG_HAS_KARCHIVE)
endif()

# Fork: faces found and recognised locally with OpenCV (YuNet + SFace) when
# OpenCV is available; without it the "faces" analysis is skipped and reports
# why (owner's answer 2026-10-30, SpecMediaIntelligence.md)
find_package(OpenCV QUIET COMPONENTS core imgproc dnn objdetect)
if (OpenCV_FOUND)
    message(STATUS "OpenCV ${OpenCV_VERSION}: face recognition enabled")
    target_include_directories(katalog-core PRIVATE ${OpenCV_INCLUDE_DIRS})
    target_link_libraries(katalog-core PRIVATE opencv_core opencv_imgproc opencv_dnn opencv_objdetect)
    target_compile_definitions(katalog-core PRIVATE TECA_HAVE_OPENCV)
else()
    message(STATUS "OpenCV not found: face recognition disabled")
endif()

# Fork (D44, owner's decision of 2026-10-31): encrypted face and people layers
# for read-only Macs. libcrypto gives AES-256-GCM, X25519, HKDF and Argon2id;
# Argon2id needs OpenSSL 3.2 or later at run time (a 3.0/3.1 build compiles,
# and the layers then report that Argon2id is missing). On macOS the reader
# key lives in the login keychain behind Touch ID or the Mac password.
find_package(OpenSSL 3.0 QUIET COMPONENTS Crypto)
if (NOT OPENSSL_FOUND AND APPLE AND NOT OPENSSL_ROOT_DIR AND NOT DEFINED ENV{OPENSSL_ROOT_DIR})
    # Homebrew's OpenSSL is keg-only on some Macs (CI runners): its own prefix
    foreach (_teca_ssl /opt/homebrew/opt/openssl /opt/homebrew/opt/openssl@3
                       /usr/local/opt/openssl /usr/local/opt/openssl@3)
        if (EXISTS "${_teca_ssl}/include/openssl/evp.h")
            set(OPENSSL_ROOT_DIR "${_teca_ssl}")
            break()
        endif()
    endforeach()
endif()
find_package(OpenSSL 3.0 REQUIRED COMPONENTS Crypto)
message(STATUS "OpenSSL ${OPENSSL_VERSION}: encrypted layers (${OPENSSL_CRYPTO_LIBRARY})")
target_sources(katalog-core PRIVATE tecacrypto.h tecacrypto.cpp platform/mackeychain.h)
target_link_libraries(katalog-core PRIVATE OpenSSL::Crypto)
if (APPLE)
    target_sources(katalog-core PRIVATE platform/mackeychain.mm)
    set_source_files_properties(platform/mackeychain.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_link_libraries(katalog-core PRIVATE
        "-framework Foundation" "-framework Security" "-framework LocalAuthentication")
else()
    target_sources(katalog-core PRIVATE platform/mackeychain_other.cpp)
endif()

target_include_directories(katalog-core
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
    PUBLIC  ${CMAKE_SOURCE_DIR}
    PUBLIC  ${KF6CoreAddons_INCLUDE_DIRS}
    PUBLIC  ${KF6FileMetaData_INCLUDE_DIRS}
)

set_target_properties(katalog-core PROPERTIES
    AUTOMOC_MOC_OPTIONS "-I${CMAKE_SOURCE_DIR}"
)
