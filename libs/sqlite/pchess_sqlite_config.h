/* SPDX-License-Identifier: GPL-3.0-or-later */
/* PocketChess build options for the SQLite amalgamation (P4, libs/README.md). Both builds compile
 * sqlite3.c with -DSQLITE_CUSTOM_INCLUDE=pchess_sqlite_config.h, so this file is the only place
 * where the options live. Based on https://sqlite.org/compile.html#recommended_compile_time_options
 * minus the ones that change API behaviour we might want later (decltype, progress callback). */
#ifndef PCHESS_SQLITE_CONFIG_H
#define PCHESS_SQLITE_CONFIG_H

#define SQLITE_THREADSAFE 0              /* one UI thread owns every connection (AGENTS.md) */
#define SQLITE_DQS 0                     /* "text" is an identifier, never a string literal */
#define SQLITE_DEFAULT_MEMSTATUS 0
#define SQLITE_DEFAULT_WAL_SYNCHRONOUS 1 /* WAL + NORMAL: durable except for the last commit on power loss */
#define SQLITE_LIKE_DOESNT_MATCH_BLOBS 1
#define SQLITE_MAX_EXPR_DEPTH 0
#define SQLITE_OMIT_DEPRECATED 1
#define SQLITE_OMIT_LOAD_EXTENSION 1     /* no dlopen: no libdl on the device, nothing to load */
#define SQLITE_OMIT_SHARED_CACHE 1
#define SQLITE_USE_ALLOCA 1
#define SQLITE_STRICT_SUBTYPE 1

#endif
