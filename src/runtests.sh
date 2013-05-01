#!/bin/bash -e

let "NUM_FAILED = 0"

for t in $@; do
    echo -n 'Running test:' $t'... '
    ./$t
    if [ $? -eq 0 ]; then
        printf "%bPASS\n" "\033[32m"
    else
        printf "%bFAIL\n" "\033[31m"
        let "NUM_FAILED++"
    fi
done

if [ $NUM_FAILED -eq 0 ]; then
    printf "%bSUCCESS\n" "\033[32m"
    exit 0
else
    printf "%b%d FAILED\n" "\033[31m" $NUM_FAILED
    exit 1
fi

