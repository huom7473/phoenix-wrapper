#!/bin/bash

# IMPORTANT: Be careful when using relative paths - the default working directory of the executable will be the one it's started in
# To generate logfiles in a specific directory other than this one, add the below line and adjust relative path(s):
# cd /path/to/log-directory
# Specify options to the mining executable through a shell script, and call the shell script here, rather than calling the miner executable directly.
./wrapper /path/to/miner-script
