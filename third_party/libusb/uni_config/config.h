/* config.h -- vendored libusb 的手工配置(单文件三平台)
 * 上游只有 msvc/config.h(#error 拒绝非MSVC)与 Xcode/config.h(darwin专用),
 * 本文件按两者合成: Windows参照msvc裁剪, POSIX参照Xcode。
 * 由 third_party/libusb/CMakeLists.txt 置于 include 路径最前。
 * 上游版本: v1.0.30 */
#ifndef UNI_LIBUSB_CONFIG_H
#define UNI_LIBUSB_CONFIG_H

#ifdef _WIN32
/* Define to the attribute for default visibility. */
#define DEFAULT_VISIBILITY /**/
/* Define to 1 to enable message logging. */
#define ENABLE_LOGGING 1
/* Define to 1 if compiling for a Windows platform. */
#define PLATFORM_WINDOWS 1
/* Define to the attribute for enabling parameter checks on printf-like functions. */
#define PRINTF_FORMAT(a, b) /**/

#elif defined(__APPLE__)
#define DEFAULT_VISIBILITY __attribute__((visibility("default")))
#define ENABLE_LOGGING 1
#define HAVE_SYS_TIME_H 1
#define HAVE_NFDS_T 1
#define PLATFORM_POSIX 1
#define PRINTF_FORMAT(a, b) __attribute__((__format__(__printf__, a, b)))
#define _GNU_SOURCE 1

#else /* Linux 及其他 POSIX */
#define DEFAULT_VISIBILITY __attribute__((visibility("default")))
#define ENABLE_LOGGING 1
#define HAVE_SYS_TIME_H 1
#define HAVE_NFDS_T 1
#define HAVE_CLOCK_MONOTONIC 1
#define HAVE_CLOCK_REALTIME 1
#define PLATFORM_POSIX 1
#define PRINTF_FORMAT(a, b) __attribute__((__format__(__printf__, a, b)))
#define _GNU_SOURCE 1
#endif

#endif
