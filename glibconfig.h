#ifndef __GLIBCONFIG_H__
#define __GLIBCONFIG_H__

#include <glib/gmacros.h>
#include <limits.h>
#include <float.h>

G_BEGIN_DECLS

#define G_MINFLOAT  FLT_MIN
#define G_MAXFLOAT  FLT_MAX
#define G_MINDOUBLE DBL_MIN
#define G_MAXDOUBLE DBL_MAX
#define G_MINSHORT  SHRT_MIN
#define G_MAXSHORT  SHRT_MAX
#define G_MAXUSHORT USHRT_MAX
#define G_MININT    INT_MIN
#define G_MAXINT    INT_MAX
#define G_MAXUINT   UINT_MAX
#define G_MINLONG   LONG_MIN
#define G_MAXLONG   LONG_MAX
#define G_MAXULONG  ULONG_MAX

typedef signed char gint8;
typedef unsigned char guint8;
typedef signed short gint16;
typedef unsigned short guint16;

#define G_GINT16_MODIFIER "h"
#define G_GINT16_FORMAT "hi"
#define G_GUINT16_FORMAT "hu"

typedef signed int gint32;
typedef unsigned int guint32;

#define G_GINT32_MODIFIER ""
#define G_GINT32_FORMAT "i"
#define G_GUINT32_FORMAT "u"

#define G_HAVE_GINT64 1
typedef signed long gint64;
typedef unsigned long guint64;

#define G_GINT64_CONSTANT(val)  (val##L)
#define G_GUINT64_CONSTANT(val) (val##UL)
#define G_GINT64_MODIFIER "l"
#define G_GINT64_FORMAT "li"
#define G_GUINT64_FORMAT "lu"

#define GLIB_SIZEOF_VOID_P  8
#define GLIB_SIZEOF_LONG    8
#define GLIB_SIZEOF_SIZE_T  8
#define GLIB_SIZEOF_SSIZE_T 8

typedef signed long gssize;
typedef unsigned long gsize;

#define G_GSIZE_MODIFIER "l"
#define G_GSSIZE_MODIFIER "l"
#define G_GSIZE_FORMAT "lu"
#define G_GSSIZE_FORMAT "li"

#define G_MAXSIZE  G_MAXULONG
#define G_MINSSIZE G_MINLONG
#define G_MAXSSIZE G_MAXLONG

typedef gint64 goffset;
#define G_MINOFFSET G_MININT64
#define G_MAXOFFSET G_MAXINT64
#define G_GOFFSET_MODIFIER G_GINT64_MODIFIER
#define G_GOFFSET_FORMAT   G_GINT64_FORMAT

#define G_POLLFD_FORMAT "%d"

#define GPOINTER_TO_INT(p)  ((gint)  (glong) (p))
#define GPOINTER_TO_UINT(p) ((guint) (gulong) (p))
#define GINT_TO_POINTER(i)  ((gpointer) (glong) (i))
#define GUINT_TO_POINTER(u) ((gpointer) (gulong) (u))

typedef signed long gintptr;
typedef unsigned long guintptr;

#define GINTPTR_FORMAT "li"
#define GINT_TO_POINTER(i)  ((gpointer) (gintptr) (i))
#define GUINT_TO_POINTER(u) ((gpointer) (guintptr) (u))
#define GPOINTER_TO_INT(p)  ((gint)  (gintptr) (p))
#define GPOINTER_TO_UINT(p) ((guint) (guintptr) (p))

#define G_MAXINT32   INT32_MAX
#define G_MAXUINT32  UINT32_MAX
#define G_MAXINT64   INT64_MAX
#define G_MAXUINT64  UINT64_MAX
#define G_MININT32   INT32_MIN
#define G_MININT64   INT64_MIN

#define G_ATOMIC_OP_MEMORY_BARRIER_NEEDED 1

#define G_DIR_SEPARATOR       '/'
#define G_DIR_SEPARATOR_S     "/"
#define G_SEARCHPATH_SEPARATOR   ':'
#define G_SEARCHPATH_SEPARATOR_S ":"
#define G_IS_DIR_SEPARATOR(c) ((c) == G_DIR_SEPARATOR || (c) == '\\')

#define G_MODULE_SUFFIX "so"
#define G_MODULE_IMPL_DL
#define G_MODULE_IMPL G_MODULE_IMPL_DL
#define G_MODULE_BASEDIR ""

#define G_THREADS_ENABLED
#define G_THREADS_IMPL_POSIX

#define G_PID_FORMAT "i"

typedef struct _GStaticMutex GStaticMutex;
struct _GStaticMutex { void *p; };

typedef struct _GStaticRecMutex GStaticRecMutex;
struct _GStaticRecMutex { GStaticMutex mutex; guint depth; };

typedef struct _GStaticRWLock GStaticRWLock;
struct _GStaticRWLock {
    GStaticMutex mutex;
    guint read_counter;
    guint write_counter;
    guint have_writer;
    guint want_to_read;
    guint want_to_write;
};

typedef struct _GStaticPrivate GStaticPrivate;
struct _GStaticPrivate { guint index; };

#define GLIB_SYSDEF_AF_UNIX  1
#define GLIB_SYSDEF_AF_INET  2
#define GLIB_SYSDEF_AF_INET6 30
#define GLIB_SYSDEF_AF_UNKNOWN -1

#define GLIB_SYSDEF_MSG_OOB        1
#define GLIB_SYSDEF_MSG_PEEK       2
#define GLIB_SYSDEF_MSG_DONTROUTE  4

#define G_OS_UNIX 1
#define G_OS_DARWIN 1

#define GLIB_HAVE_ALLOCA_H 1

#define G_VA_COPY va_copy
#define G_VA_COPY_AS_ARRAY 1

G_END_DECLS

#endif