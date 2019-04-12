# Requirements

* cmake (as build system)
* OpenSSL
* Pthreads (for POSIX systems it's builtin, for Windows there's a library)

```
git submodule init
git submodule update
mkdir _build && cd _build
cmake ../
cmake --build ./
```


