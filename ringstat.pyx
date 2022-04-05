# cython: language_level=3
# distutils: language = c
# -*- coding: UTF-8 -*-
# https://cython.readthedocs.io/en/latest/src/userguide/source_files_and_compilation.html#compiler-directives
# https://cython.readthedocs.io/en/latest/src/userguide/source_files_and_compilation.html#globally
import os

cdef extern from "libq.h":
    ctypedef struct ring:
        pass
    ring *ring_open (const char *name) nogil
    void ring_get_prepare (ring * r, const void ** buf) nogil
    void ring_get_commit (ring * r) nogil
    void ring_close (ring * r) nogil


cdef class Ring:
    cdef:
        ring* __ring

    def __cinit__(self, str name) -> None:
        self.__ring = ring_open(os.fsencode(name))

    def __dealloc__(self) -> None:
        ring_close(self.__ring)
        self.__ring = NULL

    def get(self) -> bytes:
        # https://cython.readthedocs.io/en/latest/src/tutorial/strings.html
        cdef const char* buf
        cdef bytes retval
        with nogil:
            ring_get_prepare(self.__ring, <const void**>&buf)
        try:
            retval = <bytes> buf
        finally:
            ring_get_commit(self.__ring)
        return retval
