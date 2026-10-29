if(NOT PREFIX OR NOT IS_DIRECTORY "${PREFIX}")
    message(FATAL_ERROR "A populated PREFIX is required")
endif()
file(GLOB_RECURSE workspace_metadata LIST_DIRECTORIES FALSE
    "${PREFIX}/.*.iisacc-preview" "${PREFIX}/.iisacc-temporary.lock" "${PREFIX}/.DS_Store")
if(workspace_metadata)
    message(FATAL_ERROR "Workspace metadata must not be installed: ${workspace_metadata}")
endif()
