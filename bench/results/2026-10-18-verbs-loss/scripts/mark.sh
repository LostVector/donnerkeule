#!/bin/bash
ssh "$1" 'cut -d" " -f1 /proc/uptime'
