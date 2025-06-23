#!/bin/bash
INPUT_LEDGER_LIST=run_ledger_tests_all.txt
INPUT_LEDGERS_LOCATION={path/to/ledgers}
OUTPUT_LEDGERS_LOCATION={path/to/output/ledgers}
AGAVE_REPO_DIR={path/to/agave/directory}


while [[ $# -gt 0 ]]; do
  case $1 in
    -l|--input-ledger-list)
       INPUT_LEDGER_LIST="$2"
       shift
       shift
       ;;
    -i|--input-ledgers-location)
       INPUT_LEDGERS_LOCATION="$2"
       shift
       shift
       ;;
    -o|--output-ledgers-location)
       OUTPUT_LEDGERS_LOCATION="$2"
       shift
       shift
       ;;
    -a|--agave-repo-dir)
       AGAVE_REPO_DIR="$2"
       shift
       shift
       ;;
    -*|--*)
       echo "unknown option $1"
       exit 1
       ;;
    *)
       POSITION_ARGS+=("$1")
       shift
       ;;
  esac
done

while IFS= read -r line; do
  unset entry
  declare -A entry

  args=($line)
  i=0
  while [ $i -lt ${#args[@]} ]; do
    key="${args[$i]}"
    if [[ $key == -* ]]; then
      value="${args[$((i + 1))]}"
      entry[${key:1}]="$value"
      ((i+=2))
    else
      ((i++))
    fi
  done

  if [ -n "${entry[o]}" ]; then
    echo "Skipping ledger: ${entry[l]} because one-offs are not empty"
    continue
  fi

  INPUT_LEDGER_LOCATION=${INPUT_LEDGERS_LOCATION}/${entry[l]}
  OUTPUT_LEDGER_LOCATION=${OUTPUT_LEDGERS_LOCATION}/${entry[l]}
  END_SLOT=${entry[e]}
  START_SLOT=$(echo "${entry[s]}" | cut -d'-' -f2)
  cp -r ${INPUT_LEDGER_LOCATION} ${OUTPUT_LEDGERS_LOCATION}

  CREATE_SNAPSHOT_CMD="$AGAVE_REPO_DIR/target/release/agave-ledger-tool create-snapshot ${START_SLOT} -l ${OUTPUT_LEDGER_LOCATION} --enable-capitalization-change"
  echo "$CREATE_SNAPSHOT_CMD"
  $CREATE_SNAPSHOT_CMD &> /dev/null

  SNAPSHOT_COUNT=$(ls $OUTPUT_LEDGER_LOCATION/snapshot*tar.zst 2>/dev/null | wc -l)
  if [ "$SNAPSHOT_COUNT" -gt 1 ]; then
    rm "${OUTPUT_LEDGER_LOCATION}/${entry[s]}"
    echo "Removed ${entry[s]} from ledger ${entry[l]} due to getting overwritten by newer snapshot"
  fi

  REWRITE_CMD="$AGAVE_REPO_DIR/target/release/agave-ledger-tool verify -l ${OUTPUT_LEDGER_LOCATION} --halt-at-slot ${END_SLOT} --use-snapshot-archives-at-startup when-newest --no-accounts-db-experimental-accumulator-hash --force-update-to-open"
  echo "$REWRITE_CMD"
  $REWRITE_CMD &> /dev/null

  rm -rf ${OUTPUT_LEDGER_LOCATION}/ledger_tool
  rm -rf ${OUTPUT_LEDGER_LOCATION}/snapshot
  rm -rf ${OUTPUT_LEDGER_LOCATION}/accounts
done < "$INPUT_LEDGER_LIST"
