#!/usr/bin/env bash

set -euo pipefail

readonly PROG="$(basename "$0")"

TOTAL_PARTS=""
STANDALONE_DEVICE_COUNT=""
SELECTION_DEVICE_COUNT=""
RECOMMEND_RESERVE_GB=1
DRY_RUN=true
FORCE=false
WIPE_METADATA=false
DISKS=()
PARTITION_START=""
PARTITION_END=""
PARTITION_PATHS=()

readonly FIRST_PARTITION_MIB=1
readonly END_RESERVED_MIB=1

usage() {
    cat <<EOF
Usage:
  ${PROG} --parts TOTAL --disk /dev/DISK [--disk /dev/DISK ...] [-c COUNT] [-y] [-f] [-w]

Create a GPT partition table and split each whole disk or loop block device
into an equal number of partitions. TOTAL is the total partition count across
all devices and must be divisible by the number of devices.

Options:
  -n, --parts TOTAL     Total number of partitions across all devices.
  -d, --disk PATH       Whole disk or loop block device, for example
                        /dev/nvme0n1 or /dev/loop0.
                        Repeat this option for multiple devices.
  -c, --dev-cnt COUNT
                        Device count used by the standalone disk-selection
                        algorithm when calculating the memcache SSD capacity
                        recommendation. Defaults to TOTAL. Use 0 to model
                        legacy bio.standalone.device_count = 0.
      --recommend-reserve-gb GB
                        Metadata capacity to reserve from the smallest
                        selected SSD capacity before printing the memcache
                        recommendation. Defaults to 1.
  -y, --yes             Actually write partition tables. Without this, only
                        print the commands that would be run.
  -f, --force           Allow replacing an existing partition table.
  -w, --wipe-metadata   Erase filesystem, RAID, LVM signatures, and BoostIO
                        BDM headers from old partitions before repartitioning
                        and from new partitions after repartitioning.
  -h, --help            Show this help.

Examples:
  ${PROG} --parts 3 --disk /dev/nvme0n1
  ${PROG} --parts 3 --disk /dev/loop0 -y
  ${PROG} --parts 6 --disk /dev/nvme0n1 --disk /dev/nvme1n1 -y
  ${PROG} --parts 8 --disk /dev/nvme0n1 --disk /dev/nvme1n1 -c 4 -y
  ${PROG} --parts 6 --disk /dev/nvme0n1 --disk /dev/nvme1n1 -y -f -w

WARNING:
  --yes destroys the current partition table on every device passed in.
  --wipe-metadata with --yes also destroys filesystem, RAID, LVM signatures,
  and BoostIO BDM headers on old and newly created partitions.
EOF
}

die() {
    echo "ERROR: $*" >&2
    exit 1
}

info() {
    echo "INFO: $*"
}

require_cmd() {
    command -v "$1" >/dev/null 2>&1 || die "missing required command: $1"
}

quote_cmd() {
    local arg
    printf '+'
    for arg in "$@"; do
        printf ' %q' "$arg"
    done
    printf '\n'
}

run_cmd() {
    quote_cmd "$@"
    if [[ "${DRY_RUN}" == "false" ]]; then
        "$@"
    fi
}

is_positive_integer() {
    [[ "$1" =~ ^[1-9][0-9]*$ ]]
}

is_nonnegative_integer() {
    [[ "$1" =~ ^[0-9]+$ ]]
}

canonicalize_disk() {
    local disk="$1"
    [[ -b "${disk}" ]] || die "${disk} is not a block device"
    readlink -f -- "${disk}"
}

disk_type() {
    lsblk -dn -o TYPE -- "$1" 2>/dev/null | tr -d '[:space:]'
}

is_supported_disk_type() {
    [[ "$1" == "disk" || "$1" == "loop" ]]
}

has_partitions() {
    lsblk -nr -o TYPE -- "$1" | awk 'NR > 1 && $1 == "part" { found = 1 } END { exit(found ? 0 : 1) }'
}

has_mountpoints() {
    lsblk -nr -o MOUNTPOINT -- "$1" | awk 'NF { found = 1 } END { exit(found ? 0 : 1) }'
}

disk_size_mib() {
    local bytes

    bytes="$(blockdev --getsize64 "$1")"
    printf '%s\n' "$((bytes / 1024 / 1024))"
}

device_size_bytes() {
    blockdev --getsize64 "$1"
}

join_by_colon() {
    local value=""
    local item

    for item in "$@"; do
        if [[ -z "${value}" ]]; then
            value="${item}"
        else
            value="${value}:${item}"
        fi
    done
    printf '%s\n' "${value}"
}

physical_disk_key() {
    local path="$1"
    local real_path
    local sys_path

    real_path="$(readlink -f -- "${path}")" || {
        printf '%s\n' "${path}"
        return
    }

    sys_path="$(readlink -f -- "/sys/class/block/$(basename "${real_path}")" 2>/dev/null || true)"
    if [[ -z "${sys_path}" ]]; then
        printf '%s\n' "${real_path}"
        return
    fi

    while [[ -e "${sys_path}/partition" ]]; do
        sys_path="$(dirname "${sys_path}")"
    done

    printf '%s\n' "${sys_path}"
}

wipe_partition_metadata() {
    local path="$1"

    run_cmd wipefs -a "${path}"
    run_cmd dd if=/dev/zero of="${path}" bs=1M count=1 conv=fsync
}

wipe_partition_metadata_for_disk() {
    local disk="$1"
    local phase="$2"
    local output
    local name
    local count=0

    [[ "${WIPE_METADATA}" == "true" ]] || return 0

    output="$(lsblk -nrpo NAME,TYPE -- "${disk}" | awk '$2 == "part" { print $1 }')" ||
        die "failed to list partitions for ${disk}"
    for name in ${output}; do
        wipe_partition_metadata "${name}"
        ((count += 1))
    done

    if ((count == 0)); then
        info "no ${phase} partitions found on ${disk} to wipe"
    fi
}

partition_bounds() {
    local index="$1"
    local count="$2"
    local total_mib="$3"
    local usable_mib
    local start
    local end

    usable_mib=$((total_mib - FIRST_PARTITION_MIB - END_RESERVED_MIB))
    ((usable_mib >= count)) || die "disk is too small for ${count} partitions"

    start=$((FIRST_PARTITION_MIB + ((index - 1) * usable_mib / count)))
    end=$((FIRST_PARTITION_MIB + (index * usable_mib / count)))

    if ((index == count)); then
        end=$((total_mib - END_RESERVED_MIB))
    fi

    PARTITION_START="${start}"
    PARTITION_END="${end}"
}

collect_partition_paths() {
    local expected_per_disk="$1"
    local disk
    local output
    local name
    local count

    PARTITION_PATHS=()

    for disk in "${DISKS[@]}"; do
        output="$(lsblk -nrpo NAME,TYPE -- "${disk}" | awk '$2 == "part" { print $1 }')" ||
            die "failed to list partitions for ${disk}"
        count=0

        for name in ${output}; do
            PARTITION_PATHS+=("${name}")
            ((count += 1))
        done

        if ((count != expected_per_disk)); then
            die "${disk} has ${count} partitions after partitioning, expected ${expected_per_disk}"
        fi
    done

    if ((${#PARTITION_PATHS[@]} != TOTAL_PARTS)); then
        die "found ${#PARTITION_PATHS[@]} total partitions after partitioning, expected ${TOTAL_PARTS}"
    fi
}

print_bio_disk_path_config() {
    local per_disk="$1"
    local config_value

    collect_partition_paths "${per_disk}"

    config_value="$(join_by_colon "${PARTITION_PATHS[@]}")"

    info "BoostIO disk config:"
    echo "bio.disk.path = ${config_value}"
}

print_memcache_ssd_capacity_recommendation() {
    local device_count="$1"
    local reserve_bytes=$((RECOMMEND_RESERVE_GB * 1024 * 1024 * 1024))
    local key
    local idx
    local group_index
    local disk_index_in_group
    local max_group_size=0
    local offset
    local candidate
    local best_device
    local selected_index
    local total_bytes
    local min_total_bytes=-1
    local usable_bytes
    local recommended_gb
    local selected_paths
    local path
    local group
    local group_size
    local count
    local has_best_group
    local has_candidate_group
    declare -A indexes_by_key=()
    declare -A device_has_group=()
    local -a keys=()
    local -a groups=()
    local -a assigned_indexes=()
    local -a assigned_counts=()
    local -a partition_sizes=()

    for idx in "${!PARTITION_PATHS[@]}"; do
        key="$(physical_disk_key "${PARTITION_PATHS[$idx]}")"
        indexes_by_key["${key}"]+="${idx} "
        partition_sizes[$idx]="$(device_size_bytes "${PARTITION_PATHS[$idx]}")"
    done

    mapfile -t keys < <(printf '%s\n' "${!indexes_by_key[@]}" | sort)
    for key in "${keys[@]}"; do
        groups+=("${indexes_by_key[$key]}")
        count="$(wc -w <<<"${indexes_by_key[$key]}")"
        if ((count > max_group_size)); then
            max_group_size="${count}"
        fi
    done

    for ((idx = 0; idx < device_count; idx++)); do
        assigned_indexes[$idx]=""
        assigned_counts[$idx]=0
    done

    for ((disk_index_in_group = 0; disk_index_in_group < max_group_size; disk_index_in_group++)); do
        for ((group_index = 0; group_index < ${#groups[@]}; group_index++)); do
            read -r -a group <<<"${groups[$group_index]}"
            group_size="${#group[@]}"
            if ((disk_index_in_group >= group_size)); then
                continue
            fi

            best_device="${device_count}"
            for ((offset = 0; offset < device_count; offset++)); do
                candidate=$(((group_index + disk_index_in_group + offset) % device_count))
                has_best_group="${device_has_group[${best_device}:${group_index}]:-0}"
                has_candidate_group="${device_has_group[${candidate}:${group_index}]:-0}"
                if ((best_device == device_count ||
                    assigned_counts[candidate] < assigned_counts[best_device] ||
                    (assigned_counts[candidate] == assigned_counts[best_device] &&
                        has_best_group == 1 && has_candidate_group == 0))); then
                    best_device="${candidate}"
                fi
            done

            selected_index="${group[$disk_index_in_group]}"
            assigned_indexes[$best_device]+="${selected_index} "
            assigned_counts[$best_device]=$((assigned_counts[$best_device] + 1))
            device_has_group["${best_device}:${group_index}"]=1
        done
    done

    info "Standalone disk selection capacity summary:"
    printf 'device_id\tselected_ssd_capacity_gb\tselected_partitions\n'
    for ((idx = 0; idx < device_count; idx++)); do
        read -r -a group <<<"${assigned_indexes[$idx]}"
        total_bytes=0
        selected_paths=""
        for selected_index in "${group[@]}"; do
            total_bytes=$((total_bytes + partition_sizes[selected_index]))
            path="${PARTITION_PATHS[$selected_index]}"
            if [[ -z "${selected_paths}" ]]; then
                selected_paths="${path}"
            else
                selected_paths="${selected_paths}:${path}"
            fi
        done

        if [[ -z "${selected_paths}" ]]; then
            die "standalone device ${idx} has no selected partitions"
        fi

        if ((min_total_bytes < 0 || total_bytes < min_total_bytes)); then
            min_total_bytes="${total_bytes}"
        fi
        printf '%s\t%s\t%s\n' "${idx}" "$((total_bytes / 1024 / 1024 / 1024))" "${selected_paths}"
    done

    usable_bytes=$((min_total_bytes - reserve_bytes))
    if ((usable_bytes < 0)); then
        usable_bytes=0
    fi
    recommended_gb=$((usable_bytes / 1024 / 1024 / 1024))

    info "Memcache local_server SSD capacity recommendation:"
    echo "bio.standalone.device_count = ${STANDALONE_DEVICE_COUNT}"
    echo "selection.device.count = ${device_count}"
    echo "metadata.reserve.gb = ${RECOMMEND_RESERVE_GB}"
    echo "recommended.ssd.capacity.gb = ${recommended_gb}"
    if ((recommended_gb == 0)); then
        info "recommended SSD capacity is 0GB; reduce --recommend-reserve-gb or use larger partitions"
    fi
}

parse_args() {
    while (($# > 0)); do
        case "$1" in
            -n|--parts)
                (($# >= 2)) || die "$1 requires a value"
                TOTAL_PARTS="$2"
                shift 2
                ;;
            -d|--disk)
                (($# >= 2)) || die "$1 requires a value"
                DISKS+=("$2")
                shift 2
                ;;
            -c|--dev-cnt)
                (($# >= 2)) || die "$1 requires a value"
                STANDALONE_DEVICE_COUNT="$2"
                shift 2
                ;;
            --recommend-reserve-gb)
                (($# >= 2)) || die "$1 requires a value"
                RECOMMEND_RESERVE_GB="$2"
                shift 2
                ;;
            -y|--yes)
                DRY_RUN=false
                shift
                ;;
            -f|--force)
                FORCE=true
                shift
                ;;
            -w|--wipe-metadata)
                WIPE_METADATA=true
                shift
                ;;
            -h|--help)
                usage
                exit 0
                ;;
            *)
                die "unknown argument: $1"
                ;;
        esac
    done
}

validate_inputs() {
    require_cmd awk
    require_cmd blockdev
    require_cmd basename
    require_cmd dirname
    require_cmd lsblk
    require_cmd readlink
    require_cmd sort
    require_cmd tr
    require_cmd wc

    if [[ "${DRY_RUN}" == "false" ]]; then
        require_cmd parted
        require_cmd partprobe
        if [[ "${WIPE_METADATA}" == "true" ]]; then
            require_cmd wipefs
        fi
        [[ "${EUID}" -eq 0 ]] || die "must run as root when using --yes"
    fi

    [[ -n "${TOTAL_PARTS}" ]] || die "--parts is required"
    is_positive_integer "${TOTAL_PARTS}" || die "--parts must be a positive integer"
    if [[ -z "${STANDALONE_DEVICE_COUNT}" ]]; then
        STANDALONE_DEVICE_COUNT="${TOTAL_PARTS}"
    fi
    is_nonnegative_integer "${STANDALONE_DEVICE_COUNT}" || die "--dev-cnt must be a nonnegative integer"
    is_nonnegative_integer "${RECOMMEND_RESERVE_GB}" || die "--recommend-reserve-gb must be a nonnegative integer"
    ((${#DISKS[@]} > 0)) || die "at least one --disk is required"
    ((TOTAL_PARTS >= ${#DISKS[@]})) || die "--parts must be >= number of disks"
    ((TOTAL_PARTS % ${#DISKS[@]} == 0)) || die "--parts must be divisible by number of disks"
    if ((STANDALONE_DEVICE_COUNT == 0)); then
        SELECTION_DEVICE_COUNT="${TOTAL_PARTS}"
    else
        ((STANDALONE_DEVICE_COUNT <= TOTAL_PARTS)) || die "--dev-cnt must be <= --parts"
        SELECTION_DEVICE_COUNT="${STANDALONE_DEVICE_COUNT}"
    fi
}

prepare_disks() {
    local disk
    local real_disk
    local type
    local size_mib
    declare -A seen=()
    local normalized=()

    for disk in "${DISKS[@]}"; do
        real_disk="$(canonicalize_disk "${disk}")"
        [[ -z "${seen[${real_disk}]:-}" ]] || die "duplicate disk: ${disk}"
        seen["${real_disk}"]=1

        type="$(disk_type "${real_disk}")"
        is_supported_disk_type "${type}" || die "${real_disk} has TYPE=${type:-unknown}; pass a whole disk or loop device, not a partition"

        has_mountpoints "${real_disk}" && die "${real_disk} or one of its partitions is mounted"

        if has_partitions "${real_disk}" && [[ "${FORCE}" != "true" ]]; then
            die "${real_disk} already has partitions; add --force if this is intentional"
        fi

        size_mib="$(disk_size_mib "${real_disk}")"
        ((size_mib > FIRST_PARTITION_MIB + END_RESERVED_MIB)) || die "${real_disk} has invalid size: ${size_mib}MiB"
        normalized+=("${real_disk}")
    done

    DISKS=("${normalized[@]}")
}

partition_disks() {
    local per_disk=$((TOTAL_PARTS / ${#DISKS[@]}))
    local disk
    local i
    local start
    local end
    local size_mib

    info "total partitions: ${TOTAL_PARTS}"
    info "disks: ${DISKS[*]}"
    info "partitions per disk: ${per_disk}"

    if [[ "${DRY_RUN}" == "true" ]]; then
        info "dry-run mode; add --yes to actually write partition tables"
    fi

    for disk in "${DISKS[@]}"; do
        size_mib="$(disk_size_mib "${disk}")"
        wipe_partition_metadata_for_disk "${disk}" "existing"
        run_cmd parted -s --align optimal "${disk}" mklabel gpt

        for ((i = 1; i <= per_disk; i++)); do
            partition_bounds "${i}" "${per_disk}" "${size_mib}"
            start="${PARTITION_START}"
            end="${PARTITION_END}"
            run_cmd parted -s --align optimal "${disk}" mkpart primary "${start}MiB" "${end}MiB"
            run_cmd parted -s "${disk}" name "${i}" "boostio-${i}"
        done

        run_cmd partprobe "${disk}"
    done

    if [[ "${DRY_RUN}" == "false" ]] && command -v udevadm >/dev/null 2>&1; then
        run_cmd udevadm settle
    fi

    if [[ "${WIPE_METADATA}" == "true" ]]; then
        if [[ "${DRY_RUN}" == "true" ]]; then
            info "dry-run mode; newly created partition metadata will be wiped after partitioning with --yes"
        else
            collect_partition_paths "${per_disk}"
            for path in "${PARTITION_PATHS[@]}"; do
                wipe_partition_metadata "${path}"
            done
            if command -v udevadm >/dev/null 2>&1; then
                run_cmd udevadm settle
            fi
        fi
    fi

    if [[ "${DRY_RUN}" == "false" ]]; then
        print_bio_disk_path_config "${per_disk}"
        print_memcache_ssd_capacity_recommendation "${SELECTION_DEVICE_COUNT}"
    fi
}

main() {
    parse_args "$@"
    validate_inputs
    prepare_disks
    partition_disks
}

main "$@"
