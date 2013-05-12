#!/bin/bash -e

let "NUM_FAILED = 0"

for t in $@; do
    echo -n 'Running test:' $t'... '
    ./$t
    if [ $? -eq 0 ]; then
        printf "%bPASS%b\n" "\033[32m" "\033[0m"
    else
        printf "%bFAIL%b\n" "\033[31m" "\033[0m"
        let "NUM_FAILED++"
    fi
done

if [ $NUM_FAILED -eq 0 ]; then
    printf "%bSUCCESS%b\n" "\033[32m" "\033[0m"
    exit 0
else
    printf "%b%d FAILED%b\n" "\033[31m" $NUM_FAILED "\033[0m"
    exit 1
fi

