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

# run LINE... : runs the program inside the scenario's data directory with one input line
# per argument (plus any options in $ARGS) and keeps the output, without CRs, in $WORK/out.
run() {
    (cd "$dir" && printf '%s\n' "$@" | "$BIN" --data-dir . $ARGS) >"$WORK/out.raw" 2>&1
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
    4 "" "" 5 "" "" 0 0
expect_status 0
expect "Welcome, demo."
expect "Deposited 1,250.50. Your balance is now 1,750.50."
expect "Withdrew 40.00. Your balance is now 1,710.50."
expect "Insufficient funds: your balance is 1,710.50."
expect "Statement for account 100001"
expect "#  Date              Type        Category   Account     Amount   Balance"
expect "Deposit     Salary"
expect "+1,250.50  1,750.50"
expect "Withdrawal  Groceries"
expect "   -40.00  1,710.50"
expect "Total      1,750.50      40.00  1,710.50"
expect "Goodbye."
reject "$ESC["
expect_data '^A|100001|demo|pbkdf2-sha256:600000:[0-9a-f]*:[0-9a-f]*|1710.50|'
expect_data '^T|100001|3|W|40.00|1710.50|[0-9]*|0|Groceries$'

scenario "input checking"
run 7 abc "" 2 100001 1234 \
    1 12.345 1 0 1 -5 1 1e3 1 1000000000.01 \
    7 101 7 4 0 0 0
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
expect_data '^T|100002|1|D|75.00|75.00|[0-9]*|0|General$'
# A fresh run sees what the last one saved.
run 2 100002 2468 4 "" "" 0 0
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
run 2 100002 1234 4 "" "" 0 0
expect "Welcome, hill."
expect "+10,019.00"
reject "Converted"
expect_data '^A|100002|hill|pbkdf2-sha256:600000:'

scenario "transfers"
run 1 Bob 1111 1111 \
    2 100001 1234 \
    3 100002 120 Rent y \
    3 100001 \
    3 999999 \
    3 100002 1000 \
    3 100002 5 "" n \
    0 2 100002 1111 4 "" "" 0 0
expect "Sent 120.00 to account 100002. Your balance is now 380.00."
expect "That is this account. Choose another one."
expect "There is no open account 999999."
expect "Insufficient funds: your balance is 380.00."
expect "Send 5.00 to account 100002? [y/N]: Nothing was sent."
expect "Balance: 120.00"
expect "Transfer in  Rent      100001   +120.00"
expect_data '^T|100001|2|S|120.00|380.00|[0-9]*|100002|Rent$'
expect_data '^T|100002|1|R|120.00|120.00|[0-9]*|100001|Rent$'

scenario "statement periods and CSV export"
run 2 100001 1234 \
    4 2025-13-01 "" \
    4 2025-06-02 2025-06-01 \
    4 2037-01-01 "" \
    4 "" 2037-12-31 \
    6 "" "" "" \
    6 "" "" "" n \
    6 "" "" "" y \
    6 "" "" export.csv \
    0 0
expect "Enter dates as YYYY-MM-DD, for example 2026-01-31."
expect "The period must end on or after the day it starts."
expect "Statement for account 100001, from 2037-01-01"
expect "No transactions in this period."
expect "Statement for account 100001, up to 2037-12-31"
expect_count 2 "Saved 1 transaction to statement-100001.csv."
expect "statement-100001.csv already exists. Replace it? [y/N]: Nothing was exported."
expect "Saved 1 transaction to export.csv."
checks=$((checks + 1))
if [ "$(head -n 1 "$dir/export.csv" | tr -d '\r')" != "id,date,type,category,counterparty,amount,balance" ] ||
    ! grep -q '^1,[0-9: -]*,Deposit,Seed,,500.00,500.00' "$dir/export.csv"; then
    fail "unexpected CSV export"
fi

scenario "change PIN"
run 2 100001 1234 \
    8 0000 \
    8 1234 5678 8765 \
    8 1234 5678 5678 \
    0 2 100001 1234 2 100001 5678 0 0
expect "The PIN is incorrect."
expect "The PINs do not match."
expect "Your PIN has been changed."
expect_count 1 "The account number or PIN is incorrect."
expect_count 2 "Welcome, demo."
# Wrong PINs here count towards the lockout too, and end the session.
run 2 100001 5678 8 0 8 0 8 0 1
expect_count 2 "The PIN is incorrect."
expect "Locked - try again in"
expect "You have been logged out."
reject "Amount:"

scenario "closing an account"
run 1 Bob 1111 1111 \
    2 100001 1234 \
    9 \
    2 500 "" \
    9 n \
    9 y 0000 \
    9 y 1234 \
    2 100001 1234 \
    2 100002 1111 3 100001 0 0
expect "Your balance is 500.00. Withdraw or transfer it before closing the account."
expect "The account is still open."
expect "The PIN is incorrect."
expect "Account 100001 is closed."
expect "Account 100001 was closed on"
expect "There is no open account 100001."
expect_data '^A|100001|demo|.*|0.00|[0-9]*|[0-9]*|0|0|[1-9][0-9]*$'

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
