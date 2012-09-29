project(uv C)
cmake_minimum_required(VERSION 2.6)
set(CMAKE_VERBOSE_MAKEFILE on)
set(CMAKE_BUILD_TYPE Debug)

include_directories(include include/uv-private src)

if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
else(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
  include_directories(src/unix/ev src/unix/eio)
  set(CSTDFLAG "--std=c89 -pedantic -Wall -Wextra -Wno-unused-parameter")
  set(CMAKE_C_FLAGS_DEBUG -g)
  set(CFLAGS "${CFLAGS} -D_LARGEFILE_SOURCE -D_FILE_OFFSET_BITS=64")
  set(LINKFLAGS -lm)

  set(UNIX_COMMON_SRCS
    src/unix/async.c
    src/unix/core.c
    src/unix/dl.c
    src/unix/error.c
    src/unix/fs.c
    src/unix/getaddrinfo.c
    src/unix/loop.c
    src/unix/loop-watcher.c
    src/unix/pipe.c
    src/unix/poll.c
    src/unix/process.c
    src/unix/signal.c
    src/unix/stream.c
    src/unix/tcp.c
    src/unix/thread.c
    src/unix/timer.c
    src/unix/tty.c
    src/unix/udp.c
    src/unix/uv-eio.c
  )

  set(UNIX_EV_SRC
    src/unix/ev/ev.c
  )

  set(UNIX_EIO_SRC
    src/unix/eio/eio.c
  )

  if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    set(EV_CONFIG config_linux.h)
    set(EIO_CONFIG config_linux.h)
    set(CSTDFLAG "${CSTDFLAG} -D_GNU_SOURCE")
    set(LINKFLAGS "${LINKFLAGS} -ldl -lrt")
    set(SRCS
      ${UNIX_COMMON_SRCS}
      src/unix/linux/linux-core.c
      src/unix/linux/inotify.c
      src/unix/linux/syscalls.c
    )
  endif(${CMAKE_SYSTEM_NAME} MATCHES "Linux")

  if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    set(EV_CONFIG config_darwin.h)
    set(EIO_CONFIG config_darwin.h)
    set(CFLAG "${CFLAG} -D_DARWIN_USE_64_BIT_INODE=1")
    set(LINKFLAGS "${LINKFLAGS} -framework CoreServices")
    set(SRCS
      ${UNIX_COMMON_SRCS}
      src/unix/darwin.c
      src/unix/kqueue.c
      src/unix/fsevents.c
    )
  endif(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")

  set_property(SOURCE ${SRCS} PROPERTY
    COMPILE_FLAGS "${CSTDFLAG} ${CFLAGS}")

  set_property(SOURCE ${UNIX_EV_SRC} PROPERTY
    COMPILE_FLAGS ${CFLAGS})
  set_property(SOURCE ${UNIX_EV_SRC} PROPERTY
    COMPILE_DEFINITIONS EV_CONFIG_H=\"${EV_CONFIG}\")

  set_property(SOURCE ${UNIX_EIO_SRC} PROPERTY
    COMPILE_FLAGS ${CFLAGS})
  set_property(SOURCE ${UNIX_EIO_SRC} PROPERTY
    COMPILE_DEFINITIONS EIO_CONFIG_H=\"${EIO_CONFIG}\"
                        EIO_STACKSIZE=262144
                        _GNU_SOURCE)

  add_library(uv STATIC ${SRCS} ${UNIX_EV_SRC} ${UNIX_EIO_SRC})
  set_property(TARGET uv PROPERTY LINK_FLAGS ${LINKFLAGS})
endif(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
