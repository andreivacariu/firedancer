#!/bin/bash

set -ex

DIR="$( dirname -- "${BASH_SOURCE[0]}"; )";   # Get the directory name
DIR="$( realpath -e -- "$DIR"; )";    # Resolve its full path if need be
cd $DIR/../..

OBJDIR=${OBJDIR:-build/native/gcc}
NUM_PROCESSES=${NUM_PROCESSES:-12}

if [ "$LOG_PATH" == "" ]; then
  LOG_PATH="`mktemp -d`"
else
  rm    -rf $LOG_PATH
  mkdir -pv $LOG_PATH
fi

mkdir -p dump

GIT_REF=${GIT_REF:-$(cat contrib/test/test-vectors-fixtures/test-vectors-commit-sha.txt)}

echo $GIT_REF

if [ ! -d dump/test-vectors ]; then
  cd dump
  git clone -q --depth=1 https://github.com/firedancer-io/test-vectors.git
  cd test-vectors
else
  cd dump/test-vectors
fi

git fetch -q --depth=1 origin $GIT_REF
git checkout -q $GIT_REF
cd ../..

LOG=$LOG_PATH/test_exec_block
cat contrib/test/test-vectors-fixtures/block-fixtures/*.list | xargs -P $NUM_PROCESSES -n 1000 ./$OBJDIR/unit-test/test_exec_sol_compat --log-path $LOG --wksp-page-sz 1073741824 --tile-cpus 5-6

LOG=$LOG_PATH/test_exec_syscall
cat contrib/test/test-vectors-fixtures/syscall-fixtures/*.list | xargs -P $NUM_PROCESSES -n 1000 ./$OBJDIR/unit-test/test_exec_sol_compat --log-path $LOG --wksp-page-sz 1073741824

LOG=$LOG_PATH/test_exec_interp
cat contrib/test/test-vectors-fixtures/vm-interp-fixtures/*.list | xargs -P $NUM_PROCESSES -n 1000 ./$OBJDIR/unit-test/test_exec_sol_compat --log-path $LOG --wksp-page-sz 1073741824
find dump/test-vectors/vm_interp/fixtures/v0 -type f -name '*.fix' | xargs -P $NUM_PROCESSES -n 1000 ./$OBJDIR/unit-test/test_exec_sol_compat --log-path $LOG --wksp-page-sz 1073741824
find dump/test-vectors/vm_interp/fixtures/v1 -type f -name '*.fix' | xargs -P $NUM_PROCESSES -n 1000 ./$OBJDIR/unit-test/test_exec_sol_compat --log-path $LOG --wksp-page-sz 1073741824
find dump/test-vectors/vm_interp/fixtures/v2 -type f -name '*.fix' | xargs -P $NUM_PROCESSES -n 1000 ./$OBJDIR/unit-test/test_exec_sol_compat --log-path $LOG --wksp-page-sz 1073741824
find dump/test-vectors/vm_interp/fixtures/v3 -type f -name '*.fix' | xargs -P $NUM_PROCESSES -n 1000 ./$OBJDIR/unit-test/test_exec_sol_compat --log-path $LOG --wksp-page-sz 1073741824

LOG=$LOG_PATH/test_exec_precompiles
cat contrib/test/test-vectors-fixtures/precompile-fixtures/*.list | xargs -P $NUM_PROCESSES -n 1000 ./$OBJDIR/unit-test/test_exec_sol_compat --log-path $LOG --wksp-page-sz 1073741824

LOG=$LOG_PATH/test_exec_txn
cat contrib/test/test-vectors-fixtures/txn-fixtures/*.list | xargs -P $NUM_PROCESSES ./$OBJDIR/unit-test/test_exec_sol_compat --log-path $LOG --wksp-page-sz 1073741824

zstd -df dump/test-vectors/elf_loader/fixtures/*.zst
LOG=$LOG_PATH/test_elf_loader
cat contrib/test/test-vectors-fixtures/elf-loader-fixtures/*.list | xargs -P $NUM_PROCESSES -n 1000 ./$OBJDIR/unit-test/test_exec_sol_compat --log-path $LOG --wksp-page-sz 1073741824

LOG=$LOG_PATH/test_exec_instr
cat contrib/test/test-vectors-fixtures/instr-fixtures/*.list | xargs -P $NUM_PROCESSES -n 1000 ./$OBJDIR/unit-test/test_exec_sol_compat --log-path $LOG --wksp-page-sz 1073741824

echo Test vectors success
