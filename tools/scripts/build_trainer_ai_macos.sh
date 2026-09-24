#!/bin/sh

set -eu

arm_gcc=$1
arm_objcopy=$2
input=$3
output=$4
work_dir=$(mktemp -d "${TMPDIR:-/tmp}/pokeplatinum-trainer-ai.XXXXXX")

cleanup() {
    rm -rf -- "$work_dir"
}
trap cleanup EXIT HUP INT TERM

# Apple's assembler cannot evaluate divisions of label differences. Assemble the
# data with GNU as first, extract its bytes, then wrap them in a native Mach-O
# object so the final macOS link has the expected C symbol.
"$arm_gcc" -x assembler -c "$input" -o "$work_dir/trainer_ai.elf"
"$arm_objcopy" -O binary "$work_dir/trainer_ai.elf" "$work_dir/trainer_ai.bin"

printf '.section __TEXT,__const\n.globl _gTrainerAITable\n.p2align 2\n_gTrainerAITable:\n.incbin "%s"\n' \
    "$work_dir/trainer_ai.bin" > "$work_dir/trainer_ai.s"
/usr/bin/as -o "$output" "$work_dir/trainer_ai.s"
