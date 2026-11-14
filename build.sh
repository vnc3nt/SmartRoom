#!/usr/bin/env bash
set -eo pipefail

usage() {
    printf 'Usage: bash build.sh [--clean] [--flash] [--monitor] [--all] [--port PORT]\n'
}
clean=false
flash=false
monitor=false
port=""
while [ "$#" -gt 0 ]; do
    case "$1" in
        --help|-h) usage; exit 0 ;;
        --clean) clean=true; shift ;;
        --flash) flash=true; shift ;;
        --monitor) monitor=true; shift ;;
        --all) flash=true; monitor=true; shift ;;
        --port)
            if [ "$#" -lt 2 ] || [ -z "$2" ] || [[ "$2" == --* ]]; then
                printf 'Error: --port requires a port.\n' >&2
                exit 2
            fi
            port="$2"
            shift 2
            ;;
        *) printf 'Error: unknown option: %s\n' "$1" >&2; usage >&2; exit 2 ;;
    esac
done
if { "$flash" || "$monitor"; } && [ -z "$port" ]; then
    printf 'Error: specify --port explicitly; no device is selected automatically.\n' >&2
    exit 2
fi
cd -- "$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
if [ -n "${IDF_PATH:-}" ]; then
    if [ ! -f "$IDF_PATH/export.sh" ]; then
        printf 'Error: IDF_PATH does not contain export.sh.\n' >&2
        exit 2
    fi
    source "$IDF_PATH/export.sh"
elif ! command -v idf.py >/dev/null 2>&1; then
    printf 'Error: activate ESP-IDF or set IDF_PATH explicitly.\n' >&2
    exit 2
fi
command -v idf.py >/dev/null 2>&1 || { printf 'Error: idf.py unavailable.\n' >&2; exit 2; }
idf.py --version
if "$clean"; then idf.py fullclean; fi
idf.py build
idf.py size
if "$flash"; then idf.py -p "$port" flash; fi
if "$monitor"; then idf.py -p "$port" monitor; fi
