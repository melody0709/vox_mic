# Canonical Runtime install rules. The build tree is never a supported runtime
# directory; build.bat installs this component into build/run/x64-release.

function(voxmic_install_runtime target)
    if(NOT TARGET ${target})
        message(FATAL_ERROR "voxmic_install_runtime: target '${target}' does not exist")
    endif()

    set(CMAKE_INSTALL_MESSAGE NEVER)

    # The install root is build-owned. Remove only files/directories owned by
    # the optional DPDFNet payload so switching back to RNNoise-only cannot
    # leave stale DLLs or models in the runtime manifest.
    install(CODE [=[
        set(_runtime_root "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}")
        file(REMOVE
            "${_runtime_root}/voxmic.exe"
            "${_runtime_root}/runtime-manifest.json"
            "${_runtime_root}/sherpa-onnx-c-api.dll"
            "${_runtime_root}/onnxruntime.dll"
            "${_runtime_root}/onnxruntime_providers_shared.dll")
        file(REMOVE_RECURSE
            "${_runtime_root}/models"
            "${_runtime_root}/third-party-notices")
    ]=] COMPONENT Runtime)

    install(TARGETS ${target}
        RUNTIME DESTINATION "."
        COMPONENT Runtime)

    if(VOXMIC_ENABLE_DPDFNET)
        if(EXISTS "${VOXMIC_DPDFNET_RUNTIME_DIR}/sherpa-onnx-c-api.dll")
            install(FILES
                "${VOXMIC_DPDFNET_RUNTIME_DIR}/sherpa-onnx-c-api.dll"
                "${VOXMIC_DPDFNET_RUNTIME_DIR}/onnxruntime.dll"
                "${VOXMIC_DPDFNET_RUNTIME_DIR}/onnxruntime_providers_shared.dll"
                DESTINATION "."
                COMPONENT Runtime)
        endif()
        if(EXISTS "${VOXMIC_DPDFNET_MODEL}")
            install(FILES "${VOXMIC_DPDFNET_MODEL}"
                DESTINATION "models"
                COMPONENT Runtime)
        endif()
        if(EXISTS "${CMAKE_SOURCE_DIR}/third_party/DPDFNET_THIRD_PARTY_NOTICES.txt")
            install(FILES "${CMAKE_SOURCE_DIR}/third_party/DPDFNET_THIRD_PARTY_NOTICES.txt"
                DESTINATION "third-party-notices"
                COMPONENT Runtime)
        endif()
    endif()

    set(_manifest_script "${CMAKE_CURRENT_BINARY_DIR}/WriteRuntimeManifest.cmake")
    configure_file(
        "${CMAKE_SOURCE_DIR}/cmake/WriteRuntimeManifest.cmake.in"
        "${_manifest_script}"
        @ONLY)
    install(SCRIPT "${_manifest_script}" COMPONENT Runtime)
endfunction()
