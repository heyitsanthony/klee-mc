#!/bin/bash

rename '.' '.0' readbuf*.?
cat `ls readbuf*.* | sort -n` >readbuf