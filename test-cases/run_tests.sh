#!/bin/bash

mysh=../src/mysh

pass=0
fail=0

echo "Running tests"

for input in *.txt; do
    #skip result files
    if [[ "$input" == *_result.txt || "$input" == *_out.txt ]]; then
        continue
    fi

    base=${input%.txt}
    expected="${base}_result.txt"
    output="${base}_out.txt"

    echo "Test: $base"

    #run test 
    "$mysh" < "$input" > "$output"

    #compare outputs
     if diff -q "$output" "$expected" > /dev/null; then
        echo " PASS"
        ((pass++))
        rm "$output"
    else
        echo "  FAIL"
        #echo "  Showing diff:"
        #diff "$output" "$expected"
        ((fail++))
    fi

    echo

done
rm -f "$output"
echo "-------------------------------------------"
echo "Results:"
echo "Passed: $pass"
echo "Failed: $fail"