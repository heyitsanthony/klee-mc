#!/bin/bash

#echo this should be run inside the directory containing all of the chkpt snapshots

modeldiff_s=`dirname $0`/klee_chkpts_modeldiff.sh
prepost_s=`dirname $0`/chkpts_diff_prepost_dir.sh
postklee_s=`dirname $0`/chkpts_diff_postklee_dir.sh

echo running model diffs
$modeldiff_s *

echo running preposts
$prepost_s prepost-diffs

echo running post klee
$postklee_s prepost-diffs

