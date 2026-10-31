# Copies the game data next to the executable (Windows, Linux):
#   cmake -DSRC=<repo>/data -DDST=<dir> -P copy_data.cmake
# control.ini and net.ini are rewritten by the game at runtime (Options and
# Multiplayer screens): they are only copied when missing, so a rebuild keeps
# local settings. Delete them from DST to get the defaults back.
set(USER_FILES control.ini net.ini)
file(MAKE_DIRECTORY "${DST}")
file(COPY "${SRC}/" DESTINATION "${DST}"
  PATTERN ".DS_Store" EXCLUDE
  PATTERN "control.ini" EXCLUDE
  PATTERN "net.ini" EXCLUDE)
foreach(f ${USER_FILES})
  if(EXISTS "${SRC}/${f}" AND NOT EXISTS "${DST}/${f}")
    file(COPY "${SRC}/${f}" DESTINATION "${DST}")
  endif()
endforeach()
