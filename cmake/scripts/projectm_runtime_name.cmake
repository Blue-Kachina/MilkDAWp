# cmake/scripts/projectm_runtime_name.cmake
#
# Included by deploy_projectm_runtime.cmake and check_runtime_layout.cmake.
#
# milkdawp_projectm_runtime_name(<projectm-file> <out-var>)
#
# ProjectMLibrary opens an unversioned name (projectM-4(d).dll,
# libprojectM-4(d).so, libprojectM-4(d).dylib), but on Linux/macOS the
# imported location is usually the versioned file behind a symlink chain
# (libprojectM-4.so.4.2.0, libprojectM-4.4.dylib). Returns the unversioned
# name the deployed copy gets. On Windows the two are already the same.
function(milkdawp_projectm_runtime_name projectm_file out_var)
  get_filename_component(_name "${projectm_file}" NAME)
  string(REGEX REPLACE "\\.so(\\.[0-9]+)+$" ".so" _name "${_name}")
  string(REGEX REPLACE "(\\.[0-9]+)+\\.dylib$" ".dylib" _name "${_name}")
  set(${out_var} "${_name}" PARENT_SCOPE)
endfunction()
