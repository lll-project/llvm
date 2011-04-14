# Adds version control information to the variable VERS. For
# determining the Version Control System used (if any) it inspects the
# existence of certain subdirectories under LLVM_MAIN_SRC_DIR.

function(add_version_info_from_vcs VERS)
  set(result ${${VERS}})
  if( EXISTS "${LLVM_MAIN_SRC_DIR}/.svn" )
    set(result "${result}svn")
    # FindSubversion does not work with symlinks. See PR 8437
    if( NOT IS_SYMLINK "${LLVM_MAIN_SRC_DIR}" )
      find_package(Subversion)
    endif()
    if( Subversion_FOUND )
      subversion_wc_info( ${LLVM_MAIN_SRC_DIR} Project )
      if( Project_WC_REVISION )
        set(result "${result}-r${Project_WC_REVISION}")
      endif()
    endif()
  elseif( EXISTS ${LLVM_MAIN_SRC_DIR}/.git )
    set(result "${result}git")
    # Try to get a ref-id
    find_program(git_executable NAMES git git.exe git.cmd)
    if( git_executable )
      execute_process(COMMAND ${git_executable} show-ref HEAD
                      WORKING_DIRECTORY ${LLVM_MAIN_SRC_DIR}
                      TIMEOUT 5
                      RESULT_VARIABLE git_result
                      OUTPUT_VARIABLE git_output)
      if( git_result EQUAL 0 )
        string(SUBSTRING ${git_output} 0 7 git_ref_id)
        set(result "${result}-${git_ref_id}")
      else()
        execute_process(COMMAND ${git_executable} svn log --limit=1 --oneline
                        WORKING_DIRECTORY ${LLVM_MAIN_SRC_DIR}
                        TIMEOUT 5
                        RESULT_VARIABLE git_result
                        OUTPUT_VARIABLE git_output)
        if( git_result EQUAL 0 )
          string(REGEX MATCH r[0-9]+ git_svn_rev ${git_output})
          set(result "${result}-svn-${git_svn_rev}")
        endif()
      endif()
    endif()
  endif()
  set(${VERS} ${result} PARENT_SCOPE)
endfunction(add_version_info_from_vcs)
