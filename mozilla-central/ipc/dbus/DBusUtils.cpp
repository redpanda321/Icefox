/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 40 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/*
** Copyright 2006, The Android Open Source Project
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**     http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
*/

#include "DBusUtils.h"
#include <cstdio>
#include <cstring>

#undef LOG
#if defined(MOZ_WIDGET_GONK)
#include <android/log.h>
#define LOG(args...)  __android_log_print(ANDROID_LOG_INFO, "Gonk", args);
#else
#define LOG(args...)  printf(args);
#endif

#define BLUEZ_DBUS_BASE_PATH      "/org/bluez"
#define BLUEZ_DBUS_BASE_IFC       "org.bluez"
#define BLUEZ_ERROR_IFC           "org.bluez.Error"

namespace mozilla {
namespace ipc {

void
log_and_free_dbus_error(DBusError* err, const char* function, DBusMessage* msg)
{
  if (msg) {
    LOG("%s: D-Bus error in %s: %s (%s)", function,
        dbus_message_get_member((msg)), (err)->name, (err)->message);
  }	else {
    LOG("%s: D-Bus error: %s (%s)", __FUNCTION__,
        (err)->name, (err)->message);
  }
  dbus_error_free((err));
}

typedef struct {
  DBusCallback user_cb;
  void *user;
} dbus_async_call_t;

void dbus_func_args_async_callback(DBusPendingCall *call, void *data) {

  dbus_async_call_t *req = (dbus_async_call_t *)data;
  DBusMessage *msg;

  /* This is guaranteed to be non-NULL, because this function is called only
     when once the remote method invokation returns. */
  msg = dbus_pending_call_steal_reply(call);

  if (msg) {
    if (req->user_cb) {
      // The user may not deref the message object.
      req->user_cb(msg, req->user);
    }
    dbus_message_unref(msg);
  }

  //dbus_message_unref(req->method);
  dbus_pending_call_cancel(call);
  dbus_pending_call_unref(call);
  free(req);
}

dbus_bool_t dbus_func_send_async(DBusConnection *conn,
                                 DBusMessage *msg,
                                 int timeout_ms,
                                 void (*user_cb)(DBusMessage*,
                                                 void*),
                                 void *user) {
  dbus_async_call_t *pending;
  dbus_bool_t reply = FALSE;

  // Freed at end of dbus_func_args_async_callback (becomes "req")
  pending = (dbus_async_call_t *)malloc(sizeof(dbus_async_call_t));
  DBusPendingCall *call;

  pending->user_cb = user_cb;
  pending->user = user;

  reply = dbus_connection_send_with_reply(conn, msg,
                                          &call,
                                          timeout_ms);
  if (reply) {
    dbus_pending_call_set_notify(call,
                                 dbus_func_args_async_callback,
                                 pending,
                                 NULL);
  }

  if (msg) dbus_message_unref(msg);
  return reply;
}

static dbus_bool_t dbus_func_args_async_valist(DBusConnection *conn,
                                               int timeout_ms,
                                               void (*user_cb)(DBusMessage*,
                                                               void*),
                                               void *user,
                                               const char *path,
                                               const char *ifc,
                                               const char *func,
                                               int first_arg_type,
                                               va_list args) {
  DBusMessage *msg = NULL;  
  /* Compose the command */
  msg = dbus_message_new_method_call(BLUEZ_DBUS_BASE_IFC, path, ifc, func);

  if (msg == NULL) {
    LOG("Could not allocate D-Bus message object!");
    goto done;
  }

  /* append arguments */
  if (!dbus_message_append_args_valist(msg, first_arg_type, args)) {
    LOG("Could not append argument to method call!");
    goto done;
  }

  return dbus_func_send_async(conn, msg, timeout_ms, user_cb, user);
done:
  if (msg) dbus_message_unref(msg);
  return FALSE;
}

dbus_bool_t dbus_func_args_async(DBusConnection *conn,
                                 int timeout_ms,
                                 void (*reply)(DBusMessage *, void *),
                                 void *user,
                                 const char *path,
                                 const char *ifc,
                                 const char *func,
                                 int first_arg_type,
                                 ...) {
  dbus_bool_t ret;
  va_list lst;
  va_start(lst, first_arg_type);

  ret = dbus_func_args_async_valist(conn,
                                    timeout_ms,
                                    reply, user,
                                    path, ifc, func,
                                    first_arg_type, lst);
  va_end(lst);
  return ret;
}

// If err is NULL, then any errors will be LOG'd, and free'd and the reply
// will be NULL.
// If err is not NULL, then it is assumed that dbus_error_init was already
// called, and error's will be returned to the caller without logging. The
// return value is NULL iff an error was set. The client must free the error if
// set.
DBusMessage * dbus_func_args_timeout_valist(DBusConnection *conn,
                                            int timeout_ms,
                                            DBusError *err,
                                            const char *path,
                                            const char *ifc,
                                            const char *func,
                                            int first_arg_type,
                                            va_list args) {
  
  DBusMessage *msg = NULL, *reply = NULL;
  bool return_error = (err != NULL);

  if (!return_error) {
    err = (DBusError*)malloc(sizeof(DBusError));
    dbus_error_init(err);
  }

  /* Compose the command */
  msg = dbus_message_new_method_call(BLUEZ_DBUS_BASE_IFC, path, ifc, func);

  if (msg == NULL) {
    LOG("Could not allocate D-Bus message object!");
    goto done;
  }

  /* append arguments */
  if (!dbus_message_append_args_valist(msg, first_arg_type, args)) {
    LOG("Could not append argument to method call!");
    goto done;
  }

  /* Make the call. */
  reply = dbus_connection_send_with_reply_and_block(conn, msg, timeout_ms, err);
  if (!return_error && dbus_error_is_set(err)) {
    LOG_AND_FREE_DBUS_ERROR_WITH_MSG(err, msg);
  }

done:
  if (!return_error) {
    free(err);
  }
  if (msg) dbus_message_unref(msg);
  return reply;
}

DBusMessage * dbus_func_args_timeout(DBusConnection *conn,
                                     int timeout_ms,
                                     DBusError* err,
                                     const char *path,
                                     const char *ifc,
                                     const char *func,
                                     int first_arg_type,
                                     ...) {
  DBusMessage *ret;
  va_list lst;
  va_start(lst, first_arg_type);
  ret = dbus_func_args_timeout_valist(conn, timeout_ms, err,
                                      path, ifc, func,
                                      first_arg_type, lst);
  va_end(lst);
  return ret;
}

DBusMessage * dbus_func_args(DBusConnection *conn,
                             const char *path,
                             const char *ifc,
                             const char *func,
                             int first_arg_type,
                             ...) {
  DBusMessage *ret;
  va_list lst;
  va_start(lst, first_arg_type);
  ret = dbus_func_args_timeout_valist(conn, -1, NULL,
                                      path, ifc, func,
                                      first_arg_type, lst);
  va_end(lst);
  return ret;
}

DBusMessage * dbus_func_args_error(DBusConnection *conn,
                                   DBusError *err,
                                   const char *path,
                                   const char *ifc,
                                   const char *func,
                                   int first_arg_type,
                                   ...) {
  DBusMessage *ret;
  va_list lst;
  va_start(lst, first_arg_type);
  ret = dbus_func_args_timeout_valist(conn, -1, err,
                                      path, ifc, func,
                                      first_arg_type, lst);
  va_end(lst);
  return ret;
}

int dbus_returns_int32(DBusMessage *reply) 
{
  DBusError err;
  int32_t ret = -1;

  dbus_error_init(&err);
  if (!dbus_message_get_args(reply, &err,
                             DBUS_TYPE_INT32, &ret,
                             DBUS_TYPE_INVALID)) {
    LOG_AND_FREE_DBUS_ERROR_WITH_MSG(&err, reply);
  }

  dbus_message_unref(reply);
  return ret;
}

int dbus_returns_uint32(DBusMessage *reply)
{
  DBusError err;
  uint32_t ret = -1;

  dbus_error_init(&err);
  if (!dbus_message_get_args(reply, &err,
                             DBUS_TYPE_UINT32, &ret,
                             DBUS_TYPE_INVALID)) {
    LOG_AND_FREE_DBUS_ERROR_WITH_MSG(&err, reply);
  }

  dbus_message_unref(reply);
  return ret;
}

}
}
