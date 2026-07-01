#!/bin/bash

echo "Generating 1GB test file..."
dd if=/dev/urandom of=original.dat bs=1M count=1024 status=progress
echo "Done!"
