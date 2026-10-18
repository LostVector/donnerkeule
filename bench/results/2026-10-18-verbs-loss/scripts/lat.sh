#!/bin/bash
# lat.sh NAME: us_per_message per rank/round
for h in fuzzy misty; do echo -n "$h: "; grep -h "^RESULT" $(dirname $0)/runs/$h-$1.log | sed 's/.*round=\([0-9]*\) rank=.*bytes=\([0-9]*\).*us_per_message=\(.*\)/r\1:\2B=\3/' | tr '\n' ' '; echo; done
