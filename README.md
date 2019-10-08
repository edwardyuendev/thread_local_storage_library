# Thread Local Storage Library for Threads
The goal of this project is to implement a library that provides protected memory regions for threads, which they can safely use as local storage. Threads all share the same storage which can be a problem if one thread accidentally modifies a value that another thread has stored in a variable. To protect data from being overwritten, we can provide protected local storage for the thread, which is what this project is about. This is done by implementing core functions that will allow you to create, destroy, clone, write to, and read from local storage for each thread.

# The Goals of this Project were to:
1. provide protected memory regions for threads
2. understand basic concepts of memory management
3. practice using semaphores properly
4. improve mastery over the C++ language

# Functions implemented
- tls_create()
- tls_write()
- tls_read()
- tls_destroy()
- tls_clone()
- segfault handlers, and functions to abstract code

# How To Run
1. Make sure both the Makefile and tls.cpp is in the same directory
2. Type "make" to generate the object files
3. Include tls.h in your application to be able to utilize thread local storage

# Restrictions: 
- The thread library was designed to work specifically on Linux (Fedora OS)

