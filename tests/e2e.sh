#!/bin/sh
# End-to-end tests: drive the real program with scripted input, each scenario in its own
# data directory under a scratch directory, and check what it prints and saves.
#
#   sh tests/e2e.sh build/bankbuddy
#
# Plain POSIX sh, so it runs under dash, bash (including Git Bash and MSYS2) and macOS sh.

set -u

BIN=${1:?usage: e2e.sh path/to/bankbuddy}
case $BIN in
    /* | [A-Za-z]:*) ;;
    *) BIN=$(pwd)/$BIN ;;
esac

WORK=$(mktemp -d "${TMPDIR:-/tmp}/bankbuddy-e2e.XXXXXX") || exit 1
trap 'rm -rf "$WORK"' EXIT

ESC=$(printf '\033')
checks=0
failures=0
scenario=
dir=
status=0
ARGS=

# Starts a scenario with an empty data directory.
scenario() {
    scenario=$1
    dir=$WORK/$(printf '%s' "$1" | tr -c 'A-Za-z0-9\n' '-')
    mkdir -p "$dir"
}

# run LINE... : runs the program on the scenario's data directory with one input line per
# argument (plus any options in $ARGS) and keeps the output, without CRs, in $WORK/out.
run() {
    printf '%s\n' "$@" | "$BIN" --data-dir "$dir" $ARGS >"$WORK/out.raw" 2>&1
    status=$?
    tr -d '\r' <"$WORK/out.raw" >"$WORK/out"
}

fail() {
    failures=$((failures + 1))
    printf 'FAIL [%s] %s\n' "$scenario" "$1"
    printf '%s\n' '--- output ---'
    cat "$WORK/out"
    printf '%s\n' '--------------'
}

expect() {
    checks=$((checks + 1))
    grep -qF -- "$1" "$WORK/out" || fail "expected output to contain: $1"
}

reject() {
    checks=$((checks + 1))
    if grep -qF -- "$1" "$WORK/out"; then fail "output should not contain: $1"; fi
}

# expect_count N TEXT : TEXT appears on exactly N lines.
expect_count() {
    checks=$((checks + 1))
    n=$(grep -cF -- "$2" "$WORK/out")
    [ "$n" -eq "$1" ] || fail "expected $1 line(s) containing '$2', found $n"
}

expect_status() {
    checks=$((checks + 1))
    [ "$status" -eq "$1" ] || fail "expected exit status $1, got $status"
}

# expect_data PATTERN : the data file has a line matching the basic regular expression.
expect_data() {
    checks=$((checks + 1))
    grep -q -- "$1" "$dir/bankbuddy.db" 2>/dev/null || fail "data file has no line matching: $1"
}

# ---------------------------------------------------------------------------------------

scenario "command line"
ARGS=--help
run
expect_status 0
expect "Usage: bankbuddy"
expect "--data-dir DIR"
ARGS=--bogus
run
expect_status 2
expect "unknown or incomplete option '--bogus'"
ARGS=

scenario "demo account and everyday banking"
run 2 100001 1234 \
    1 "1,250.50" Salary \
    2 40 Groceries \
    2 99999 \
    3 4 0 0
expect_status 0
expect "Welcome, demo."
expect "Deposited 1,250.50. Your balance is now 1,750.50."
expect "Withdrew 40.00. Your balance is now 1,710.50."
expect "Insufficient funds: your balance is 1,710.50."
expect "Statement for account 100001"
expect "Deposit     Salary     +1,250.50  1,750.50"
expect "Withdrawal  Groceries     -40.00  1,710.50"
expect "Total      1,750.50      40.00  1,710.50"
expect "Goodbye."
reject "$ESC["
expect_data '^A|100001|demo|pbkdf2-sha256:600000:[0-9a-f]*:[0-9a-f]*|1710.50|'
expect_data '^T|100001|3|W|40.00|1710.50|[0-9]*|Groceries$'

scenario "input checking"
run 7 abc "" 2 100001 1234 \
    1 12.345 1 0 1 -5 1 1e3 1 1000000000.01 \
    5 101 5 4 0 0 0
expect_count 3 "Please enter a number from 0 to 2."
expect_count 2 "The amount must be between 0.01 and 1,000,000,000.00."
expect_count 3 "Enter an amount like 25, 25.50 or 1,250.50."
expect "Enter a percentage from 0 to 100 with up to two decimals."
expect "Enter a whole number from 1 to 365."
expect_data '^A|100001|demo|.*|500.00|'

scenario "sign up and log back in"
run 1 "Alice Smith" 1234 4321 \
    1 "Alice Smith" 12 \
    1 "!nvalid" \
    1 "Alice Smith" 2468 2468 \
    2 100002 2468 1 75 "" 0 0
expect "The PINs do not match."
expect "A PIN must be 4 to 8 digits."
expect "Use 1-63 letters, digits, spaces"
expect "Account 100002 created."
expect "Welcome, Alice Smith."
expect "Deposited 75.00. Your balance is now 75.00."
expect_data '^A|100002|Alice Smith|pbkdf2-sha256:600000:'
expect_data '^T|100002|1|D|75.00|75.00|[0-9]*|General$'
# A fresh run sees what the last one saved.
run 2 100002 2468 3 0 0
expect "Balance: 75.00"
expect "+75.00"

scenario "login failures look the same"
run 2 100001 9999 2 999999 1234 2 abc 1234 0
expect_count 3 "The account number or PIN is incorrect."
reject "Welcome"

scenario "lockout"
run 2 100001 1111 2 100001 2222 2 100001 3333 2 100001 1234 0
expect_count 3 "The account number or PIN is incorrect."
expect "Too many failed attempts. Locked - try again in"
reject "Welcome"
# The lockout survives a restart.
run 2 100001 1234 0
expect "Locked - try again in"

scenario "end of input"
run 2 100001 1234 1
expect_status 0
expect "Amount:"

scenario "legacy data is converted and its PIN hashes upgraded"
printf '%s\r\n' '100002|hill|4257489661|10019.00|1758143817|1758143875' \
    'garbage' >"$dir/accounts.db"
printf '%s\r\n' '100002|1|D|10019.00|10019.00|1758143852|usd' >"$dir/transactions.db"
run 0
expect "Converted data from accounts.db and transactions.db to bankbuddy.db."
expect "Warning: 1 unreadable line(s) in the old files were skipped."
expect_data '^A|100002|hill|fnv1a:4257489661|10019.00|'
run 2 100002 1234 3 0 0
expect "Welcome, hill."
expect "+10,019.00"
reject "Converted"
expect_data '^A|100002|hill|pbkdf2-sha256:600000:'

scenario "a damaged data file is left alone"
printf 'BANKBUDDY 2\nnot a record\n' >"$dir/bankbuddy.db"
cp "$dir/bankbuddy.db" "$WORK/damaged.copy"
run 0
expect_status 1
expect "line 2"
expect "The data file is damaged"
checks=$((checks + 1))
cmp -s "$dir/bankbuddy.db" "$WORK/damaged.copy" || fail "the damaged file was modified"

# ---------------------------------------------------------------------------------------

printf 'e2e          %s\n' "$([ "$failures" -eq 0 ] && echo ok || echo FAILED)"
printf '%d checks, %d failed\n' "$checks" "$failures"
[ "$failures" -eq 0 ]
