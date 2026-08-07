if(NOT DEFINED FOREVERTAS_SOURCE_DIR)
    message(FATAL_ERROR "FOREVERTAS_SOURCE_DIR is required")
endif()

file(READ
    "${FOREVERTAS_SOURCE_DIR}/packaging/release/package-linux.sh"
    linux_script)
file(READ
    "${FOREVERTAS_SOURCE_DIR}/packaging/release/package-windows.ps1"
    windows_script)

foreach(platform IN ITEMS linux windows)
    set(script "${${platform}_script}")
    if(NOT script MATCHES "cache_schema=cuda-search-object-v[0-9]+" OR
       NOT script MATCHES "validator=" OR
       NOT script MATCHES "architectures=" OR
       NOT script MATCHES "split_compile_jobs=")
        message(FATAL_ERROR
            "${platform} CUDA search cache omits a source or toolchain key")
    endif()
    if(platform STREQUAL "windows" AND
       NOT script MATCHES "empty_air_certificate=OFF")
        message(FATAL_ERROR
            "windows CUDA search cache omits the empty-air feature key")
    endif()
    if(script MATCHES "Get-FileHash[^\n]*CMakeLists.txt" OR
       script MATCHES "sha256sum[^\n]*CMakeLists.txt")
        message(FATAL_ERROR
            "${platform} CUDA search cache depends on unrelated CMake edits")
    endif()
endforeach()
