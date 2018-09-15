#!/bin/bash

autoreconf --install --symlink || exit 1

echo
echo "----------------------------------------------------------------"
echo "Initialized build system. For a common configuration please run:"
echo "----------------------------------------------------------------"
echo
echo "./configure --with-deadline"
echo
