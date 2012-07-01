#!/bin/bash

grep Array ptrexprs/*smt | grep reg_ | cut -f1 -d':' | xargs rm