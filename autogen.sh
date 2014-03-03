#!/bin/bash

autoreconf --install --symlink

echo
echo "----------------------------------------------------------------"
echo "Initialized build system. For a common configuration please run:"
echo "----------------------------------------------------------------"
echo
echo "./configure --with-deadline --with-json"
echo
