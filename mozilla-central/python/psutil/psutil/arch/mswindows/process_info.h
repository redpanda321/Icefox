/*
 * $Id: process_info.h 1142 2011-10-05 18:45:49Z g.rodola $
 *
 * Copyright (c) 2009, Jay Loden, Giampaolo Rodola'. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Helper functions related to fetching process information. Used by _psutil_mswindows
 * module methods.
 */

#include <Python.h>
#include <windows.h>

HANDLE handle_from_pid_waccess(DWORD pid, DWORD dwDesiredAccess);
HANDLE handle_from_pid(DWORD pid);
PVOID GetPebAddress(HANDLE ProcessHandle);
HANDLE handle_from_pid(DWORD pid);
BOOL is_running(HANDLE hProcess);
int pid_in_proclist(DWORD pid);
int pid_is_running(DWORD pid);
PyObject* get_arg_list(long pid);
PyObject* get_ppid(long pid);
PyObject* get_name(long pid);
DWORD* get_pids(DWORD *numberOfReturnedPIDs);
