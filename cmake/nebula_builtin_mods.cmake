# NebulaCoop's built-in mods (docs/COOP_PLAN.md "Built-in mods"), as sm64coopdx has its built-in mods. They're made in
# LumaLink-coop (the `lumalink` submodule), which packs each one as builtin/<id>.lumacoop (its tests fail while one is
# stale). The runtime carries every file of each package, and galaxy::mods::install_builtin_mods writes them into the
# mods folder. Nothing is copied by hand: a new version of a built-in mod is a new submodule commit.
#
# nebula_builtin_mods(<nebula root> <output dir>) reads them at configure time (a changed package configures the tree
# again) and writes <output dir>/nebula_builtin_mods.inc, which runtime/src/mods.cpp includes. The include path is set
# on mods.cpp in the calling directory, so call it where the targets that build mods.cpp are made.

# One file's bytes as a C array named `name`, appended to `text_var`.
function(_nebula_builtin_bytes text_var name file)
    file(READ "${file}" hex HEX)
    string(LENGTH "${hex}" digits)
    if(digits EQUAL 0)
        set(body "0")
    else()
        string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," body "${hex}")
        # Lines of 32 bytes.
        string(REGEX REPLACE "((0x[0-9a-f][0-9a-f],){32})" "\\1\n    " body "${body}")
    endif()
    set(${text_var} "${${text_var}}static const unsigned char ${name}[] = {\n    ${body}\n};\n" PARENT_SCOPE)
endfunction()

function(nebula_builtin_mods root out_dir)
    # As the targets name mods.cpp, so its properties are found.
    cmake_path(ABSOLUTE_PATH root NORMALIZE)
    if(root MATCHES ".../$")
        string(REGEX REPLACE "/$" "" root "${root}")
    endif()
    set(builtin "${root}/lumalink/builtin")
    file(GLOB packs CONFIGURE_DEPENDS "${builtin}/*.lumacoop")
    if(NOT packs)
        message(FATAL_ERROR "NebulaCoop's built-in mods aren't in ${builtin}: the lumalink submodule is missing. "
                            "Run: git submodule update --init lumalink")
    endif()
    list(SORT packs)
    set(work "${out_dir}/builtin_mods")
    file(REMOVE_RECURSE "${work}")
    set(arrays "")
    set(rows "")
    set(n 0)
    foreach(pack IN LISTS packs)
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${pack}")
        # The id is the package's name (builtin/<id>.lumacoop), as the mods folder names the mod.
        get_filename_component(id "${pack}" NAME_WE)
        if(NOT id MATCHES "^[a-z0-9_-]+$")
            message(FATAL_ERROR "A built-in mod's package must be named by its id: ${pack}")
        endif()
        file(ARCHIVE_EXTRACT INPUT "${pack}" DESTINATION "${work}/${id}")
        file(GLOB_RECURSE files LIST_DIRECTORIES false RELATIVE "${work}/${id}" "${work}/${id}/*")
        list(SORT files)
        if(NOT "luma.json" IN_LIST files)
            message(FATAL_ERROR "A built-in mod's package has no luma.json: ${pack}")
        endif()
        foreach(rel IN LISTS files)
            file(SIZE "${work}/${id}/${rel}" bytes)
            _nebula_builtin_bytes(arrays "kBuiltinBytes${n}" "${work}/${id}/${rel}")
            string(APPEND rows "    {\"${id}\", \"${rel}\", ${bytes}u, kBuiltinBytes${n}},\n")
            math(EXPR n "${n} + 1")
        endforeach()
    endforeach()
    set(text "// Written by cmake/nebula_builtin_mods.cmake from lumalink/builtin: do not edit.\n")
    string(APPEND text "${arrays}\n")
    string(APPEND text "constexpr BuiltinFile kBuiltinFiles[] = {\n${rows}};\n")
    set(path "${out_dir}/nebula_builtin_mods.inc")
    set(old "")
    if(EXISTS "${path}")
        file(READ "${path}" old)
    endif()
    if(NOT old STREQUAL text)
        file(WRITE "${path}" "${text}")
    endif()
    set_property(SOURCE "${root}/runtime/src/mods.cpp" APPEND PROPERTY INCLUDE_DIRECTORIES "${out_dir}")
    set_property(SOURCE "${root}/runtime/src/mods.cpp" APPEND PROPERTY OBJECT_DEPENDS "${path}")
endfunction()
