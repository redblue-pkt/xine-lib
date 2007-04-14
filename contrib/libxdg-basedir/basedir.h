/* Copyright (c) 2007 Mark Nevill
 * 
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 * 
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

/** @file basedir.h
  * @brief Functions for using the XDG basedir specification.
  * See http://standards.freedesktop.org/basedir-spec/basedir-spec-0.6.html for details. */ 

#ifndef XDG_BASEDIR_H
#define XDG_BASEDIR_H

#if !defined(__cplusplus) && !defined(__bool_true_false_are_defined) && !defined(_DOXYGEN)
#if defined(STDC_HEADERS) || defined(HAVE_STDBOOL_H)
#include <stdbool.h>
#else
#ifdef HAVE__BOOL
#define bool _Bool
#else
#define bool int
#endif // HAVE__BOOL
#define true 1
#define false 0
#define XDG_BASEDIR_H_LOCAL_BOOL_DEFINE
#define __bool_true_false_are_defined
#endif // STDC_HEADERS || HAVE_STDBOOL_H
#endif // !__cplusplus && !__bool_true_false_are_defined

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Version of XDG basedir-spec implemented in this library. */
#define XDG_BASEDIR_SPEC 0.6

/** @name XDG data cache management */
/*@{*/

/** Handle to XDG data cache.
  * Handles are allocated with xdgAllocHandle() and
  * freed with xdgFreeHandle(). */
typedef struct /*_xdgHandle*/ {
	void *reserved;
} *xdgHandle;



/** Get a handle to an XDG data cache and initialize the cache.
  * Use xdgFreeHandle() to free the handle. */
xdgHandle xdgAllocHandle();

/** Free handle to XDG data cache.
  * Free handle allocated using xdgAllocHandle(). */
void xdgFreeHandle(xdgHandle handle);

/** Update the data cache.
  * This should not be done frequently as it reallocates the cache.
  * Even if updating the cache fails the handle remains valid and can
  * be used to access XDG data as it was before xdgUpdateData() was called.
  * @return 0 if update failed, non-0 if successful.*/
bool xdgUpdateData(xdgHandle handle);

/*@}*/
/** @name Basic XDG-Basedir Queries */
/*@{*/

/** Base directory for user specific data files.
  * "${XDG_DATA_HOME:-$HOME/.local/share}" */
const char * xdgDataHome(xdgHandle handle);

/** Base directory for user specific configuration files.
  * "${XDG_CONFIG_HOME:-$HOME/.config}" */
const char * xdgConfigHome(xdgHandle handle);

/** Preference-ordered set of base directories to search for data files
  * in addition to the $XDG_DATA_HOME base directory.
  * "${XDG_DATA_DIRS:-/usr/local/share/:/usr/share/}".
  * @return A null-terminated list of directory strings. */
const char * const * xdgDataDirectories(xdgHandle handle);

/** Preference-ordered set of base directories to search for data files
  * with $XDG_DATA_HOME prepended.
  * The base directory defined by $XDG_DATA_HOME is considered more
  * important than any of the base directories defined by $XDG_DATA_DIRS.
  * @return A null-terminated list of directory strings. */
const char * const * xdgSearchableDataDirectories(xdgHandle handle);

/** Preference-ordered set of base directories to search for configuration
  * files in addition to the $XDG_CONFIG_HOME base directory.
  * "${XDG_CONFIG_DIRS:-/etc/xdg}".
  * @return A null-terminated list of directory strings. */
const char * const * xdgConfigDirectories(xdgHandle handle);

/** Preference-ordered set of base directories to search for configuration
  * files with $XDG_CONFIG_HOME prepended.
  * The base directory defined by $XDG_CONFIG_HOME is considered more
  * important than any of the base directories defined by $XDG_CONFIG_DIRS.
  * @return A null-terminated list of directory strings. */
const char * const * xdgSearchableConfigDirectories(xdgHandle handle);

/** Base directory for user specific non-essential data files.
  * "${XDG_CACHE_HOME:-$HOME/.cache}" */
const char * xdgCacheHome(xdgHandle handle);

/*@}*/

/** @name Higher-level XDG-Basedir Queries */
/*@{*/

/** Find all existing data files corresponding to relativePath.
  * Consider as performing @code fopen(filename, "r") @endcode on every possible @c filename
  * 	and returning the successful <tt>filename</tt>s.
  * @param relativePath Path to scan for.
  * @param handle Handle to data cache.
  * @return A sequence of null-terminated strings terminated by a double-null (empty string)
  * 	and allocated using malloc(), e.g.: @code "/etc/share\0/home/jdoe/.local\0" @endcode
  */
const char * xdgDataFind(const char* relativePath, xdgHandle handle);

/** Find all existing config files corresponding to relativePath.
  * Consider as performing @code fopen(filename, "r") @endcode on every possible @c filename
  * 	and returning the successful <tt>filename</tt>s.
  * @param relativePath Path to scan for.
  * @param handle Handle to data cache.
  * @return A sequence of null-terminated strings terminated by a double-null (empty string)
  * 	and allocated using malloc(), e.g.: @code "/etc/xdg\0/home/jdoe/.config\0" @endcode
  */
const char * xdgConfigFind(const char* relativePath, xdgHandle handle);

/** Open first possible data file corresponding to relativePath.
  * Consider as performing @code fopen(filename, mode) @endcode on every possible @c filename
  * 	and returning the first successful @c filename or @c NULL.
  * @param relativePath Path to scan for.
  * @param mode Mode with which to attempt to open files (see fopen modes).
  * @param handle Handle to data cache.
  * @return File pointer if successful else @c NULL. Client must use @c fclose to close file.
  */
FILE * xdgDataOpen(const char* relativePath, const char* mode, xdgHandle handle);

/** Open first possible config file corresponding to relativePath.
  * Consider as performing @code fopen(filename, mode) @endcode on every possible @c filename
  * 	and returning the first successful @c filename or @c NULL.
  * @param relativePath Path to scan for.
  * @param mode Mode with which to attempt to open files (see fopen modes).
  * @param handle Handle to data cache.
  * @return File pointer if successful else @c NULL. Client must use @c fclose to close file.
  */
FILE * xdgConfigOpen(const char* relativePath, const char* mode, xdgHandle handle);

/*@}*/

#ifdef __cplusplus
} // extern "C"
#endif

#ifdef XDG_BASEDIR_H_LOCAL_BOOL_DEFINE
#undef bool
#undef true
#undef false
#undef __bool_true_false_are_defined
#undef XDG_BASEDIR_H_LOCAL_BOOL_DEFINE
#endif

#endif /*XDG_BASEDIR_H*/
