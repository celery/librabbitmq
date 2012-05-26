#ifndef __PYLIBRABBIT_UTIL_H__
#define __PYLIBRABBIT_UTIL_H__

#include <Python.h>
#include <stddef.h>

#define PY25
#ifdef PY25
#  define PY_SSIZE_T_CLEAN
#  define PY_SIZE_TYPE Py_ssize_t
#  define PYINT_FROM_SSIZE_T PyInt_FromSsize_t
#  define PYINT_AS_SSIZE_T PyInt_AsSsize_t
#else
#  define PY_SIZE_TYPE long
#  define PYINT_FROM_SSIZE_T PyInt_FromLong
#  define PYINT_AS_SSIZE_T PyInt_AsLong
#endif

#if PY_VERSION_HEX >= 0x03000000
#  define FROM_FORMAT PyUnicode_FromFormat
#  define PyInt_FromLong PyLong_FromLong
#  define PyInt_FromSsize_t PyLong_FromSsize_t
#else
#  define FROM_FORMAT PyString_FromFormat
#endif

#ifndef _PYRMQ_INLINE
# if __GNUC__ && !__GNUC_STDC_INLINE__
#  define _PYRMQ_INLINE extern inline
# else
#  define _PYRMQ_INLINE inline
# endif
#endif

#endif /* __PYLIBRABBIT_UTIL_H__ */
