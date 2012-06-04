#!/bin/bash
kopt -pipe-solver -dump-bin -check-rule  proofs >pending.brule
kopt -pipe-solver -brule-xtive -rule-file=pending.brule
kopt -pipe-solver -brule-rebuild -rule-file=pending.brule pending.rebuild.brule
mv pending.rebuild.brule pending.brule
kopt -rule-file=pending.brule -max-stp-time=30 -pipe-solver -db-punchout punched.brule.tmp
mv punched.brule.tmp punched.brule