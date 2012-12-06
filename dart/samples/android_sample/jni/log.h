// Copyright (c) 2012, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#ifndef LOG_H
#define LOG_H

#include <android/log.h>

#define LOGX(LOG_LEVEL, ...) do { \
    __android_log_print(LOG_LEVEL, "DartExt", __VA_ARGS__);	\
  } while (0)
#define LOGI(...) LOGX(ANDROID_LOG_INFO, __VA_ARGS__)
#define LOGE(...) LOGX(ANDROID_LOG_ERROR, __VA_ARGS__)

#endif
