# Building and Installing librdpmux

librdpmux has been tested to work on Fedora 23, Ubuntu Linux 16.04, and Arch Linux. It should work on any other Linux distribution of similar vintage. It probably also works on older Linux distributions as well, but we haven't tested on them. Feel free to share your success and/or horror stories with us and we'll work to improve coverage.

## Dependencies
librdpmux depends on the following libraries:

1. Nanomsg 0.6
2. Pixman
3. GLib 2.0

## Installation

librdpmux uses CMake as its build system. To build and install, simply run

```bash
cmake .
make
sudo make install
```
