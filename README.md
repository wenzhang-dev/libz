
# libz

libz is intended to be a modern c++ codebase.

## build

```
git clone git@github.com:wenzhang-dev/libz.git
cd libz
git submodule update --init --recursive
git submodule foreach -q --recursive 'git checkout $(git config -f $toplevel/.gitmodules submodule.$name.tag)'
mkdir build && cd build
cmake .. && make -j8
```

