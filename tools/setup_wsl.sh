#!/usr/bin/env bash
set -euo pipefail

prefix=/usr/local/pspdev
url=https://github.com/pspdev/pspdev/releases/latest/download/pspdev-ubuntu-latest-x86_64.tar.gz
update=false
dry_run=false

usage() {
    echo 'Usage: bash tools/setup_wsl.sh [--update] [--prefix DIR] [--url URL] [--dry-run]'
}

die() { echo "ERROR: $*" >&2; exit 1; }
warn() { echo "WARNING: $*" >&2; }
plan() { printf '[dry-run]'; printf ' %q' "$@"; printf '\n'; }

while (($#)); do
    case "$1" in
        --update) update=true; shift ;;
        --dry-run) dry_run=true; shift ;;
        --prefix|--url)
            (($# >= 2)) && [[ -n $2 && $2 != --* ]] || die "Missing value for $1"
            case "$1" in
                --prefix) prefix=$2 ;;
                --url) url=$2 ;;
            esac
            shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) usage >&2; die "Unknown argument: $1" ;;
    esac
done

# PSPDEV не поддерживает пробелы и специальные символы в своём пути.
[[ $prefix =~ ^/[a-zA-Z0-9_./-]+$ ]] || die '--prefix must be an absolute Linux path without spaces or special characters'
[[ ! -L $prefix ]] || die 'Prefix must not be a symbolic link'
prefix=$(realpath -m -- "$prefix")
case "$prefix" in
    /|/usr|/usr/local|/etc|/bin|/sbin|/lib|/lib64|/home|/root|/tmp|/var|/opt)
        die "Unsafe installation prefix: $prefix" ;;
esac
[[ ! -L $prefix && ! -L $prefix.bak ]] || die 'Prefix and backup must not be symbolic links'
parent=$(dirname -- "$prefix")
base=$(basename -- "$prefix")

os_id=unknown
os_version=unknown
if [[ -r /etc/os-release ]]; then
    os_id=$(. /etc/os-release; printf '%s' "${ID:-unknown}")
    os_version=$(. /etc/os-release; printf '%s' "${VERSION_ID:-unknown}")
fi
[[ $os_id == ubuntu && $os_version == 24.04 ]] || warn "Expected Ubuntu 24.04; found $os_id $os_version. Continuing."
if ! grep -qi microsoft /proc/sys/kernel/osrelease /proc/version 2>/dev/null; then
    warn 'WSL was not detected. Continuing.'
fi
if [[ $(uname -m) != x86_64 ]]; then
    warn 'The default archive requires x86_64; use --url for your architecture.'
fi

as_root() {
    if ((EUID == 0)); then "$@"; else sudo -- "$@"; fi
}

# Для пользовательского --prefix sudo не нужен; проверяем ближайшего предка.
for_path() {
    local target=$1
    shift
    while [[ ! -e $target ]]; do target=$(dirname -- "$target"); done
    if ((EUID == 0)) || [[ -w $target && -x $target ]]; then
        "$@"
    else
        as_root "$@"
    fi
}

packages=(
    cmake ninja-build g++ libsdl2-dev python3 python3-pil git git-lfs
    build-essential pkgconf libreadline8t64 libusb-0.1-4 libgpgme11t64
    libarchive-tools fakeroot curl ca-certificates tar gzip
)
missing=()
present=()
for package in "${packages[@]}"; do
    if dpkg -s "$package" 2>/dev/null | grep -qx 'Status: install ok installed'; then
        present+=("$package")
    else
        missing+=("$package")
    fi
done
if ((${#missing[@]})); then
    if $dry_run; then
        plan as_root apt-get update
        plan as_root apt-get install -y "${missing[@]}"
    else
        as_root apt-get update
        as_root apt-get install -y "${missing[@]}"
    fi
else
    echo 'Packages: all already installed.'
fi

temp_dir=
stage=
old_moved=false
new_active=false
committed=false
cleanup() {
    local result=$? rollback_ok=true
    trap - EXIT INT TERM
    if [[ -n $stage ]]; then
        if ! $committed; then
            warn 'Installation failed; restoring the previous PSPDEV.'
            if $new_active; then
                for_path "$parent" mv -T -- "$prefix" "$stage/failed" || rollback_ok=false
            fi
            if $old_moved; then
                for_path "$parent" mv -T -- "$prefix.bak" "$prefix" || rollback_ok=false
            fi
            if [[ -e $stage/previous-backup ]]; then
                for_path "$parent" mv -T -- "$stage/previous-backup" "$prefix.bak" || rollback_ok=false
            fi
        fi
        if $rollback_ok; then
            for_path "$parent" rm -rf -- "$stage" || result=1
        else
            warn "Rollback incomplete. Preserved recovery files: $stage and $prefix.bak"
            result=1
        fi
    fi
    if [[ -n $temp_dir ]]; then rm -rf -- "$temp_dir" || result=1; fi
    exit "$result"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

pspdev_status='already installed'
if [[ -f $prefix/bin/psp-gcc ]] && ! $update; then
    echo "PSPDEV: already installed at $prefix; download skipped."
else
    pspdev_status=installed
    if [[ -e $prefix ]]; then pspdev_status=updated; fi
    if $dry_run; then
        plan curl -fL --retry 3 "$url" -o '<temporary directory>/pspdev.tar.gz'
        echo "[dry-run] Extract and validate pspdev/bin/psp-gcc and bin/psp-cmake."
        echo "[dry-run] Stage beside $prefix; rename old installation to $prefix.bak, activate new; roll back on failure."
    else
        temp_dir=$(mktemp -d)
        curl -fL --retry 3 "$url" -o "$temp_dir/pspdev.tar.gz"
        mkdir -- "$temp_dir/extracted"
        tar -xzf "$temp_dir/pspdev.tar.gz" --no-same-owner --no-same-permissions -C "$temp_dir/extracted"
        [[ -f $temp_dir/extracted/pspdev/bin/psp-gcc && -x $temp_dir/extracted/pspdev/bin/psp-gcc ]] || die 'Archive does not contain executable pspdev/bin/psp-gcc'
        [[ -x $temp_dir/extracted/pspdev/bin/psp-cmake ]] || die 'Archive does not contain executable pspdev/bin/psp-cmake'
        for_path "$parent" mkdir -p -- "$parent"
        stage=$(for_path "$parent" mktemp -d "$parent/.${base}.stage.XXXXXX")
        for_path "$parent" chmod 755 -- "$stage"
        for_path "$stage" cp -a -- "$temp_dir/extracted/pspdev" "$stage/new"
        if [[ -e $prefix.bak ]]; then
            for_path "$parent" mv -T -- "$prefix.bak" "$stage/previous-backup"
        fi
        if [[ -e $prefix ]]; then
            for_path "$parent" mv -T -- "$prefix" "$prefix.bak"
            old_moved=true
        fi
        for_path "$parent" mv -T -- "$stage/new" "$prefix"
        new_active=true
    fi
fi

export PSPDEV="$prefix"
export PATH="$PSPDEV/bin:$PATH"
profile=/etc/profile.d/pspdev.sh
profile_content=$(printf 'export PSPDEV=%s\nexport PATH="$PATH:$PSPDEV/bin"\n' "$prefix")
profile_status='already correct'
if [[ -f $profile ]] && cmp -s -- "$profile" <(printf '%s\n' "$profile_content"); then
    echo "Profile: $profile already correct."
else
    profile_status=written
    if $dry_run; then
        echo "[dry-run] Write $profile only because its content differs:"
        printf '%s\n' "$profile_content"
    else
        if [[ -z $temp_dir ]]; then temp_dir=$(mktemp -d); fi
        printf '%s\n' "$profile_content" > "$temp_dir/pspdev.sh"
        for_path /etc/profile.d install -d -m 755 /etc/profile.d
        for_path /etc/profile.d install -m 644 -- "$temp_dir/pspdev.sh" "$profile"
    fi
fi

if $dry_run; then
    plan git lfs install
    plan "$prefix/bin/psp-gcc" --version
    plan "$prefix/bin/psp-cmake" --version
    plan cmake --version
    plan ninja --version
    plan python3 -c 'import PIL'
else
    git lfs install
    "$prefix/bin/psp-gcc" --version
    "$prefix/bin/psp-cmake" --version
    cmake --version
    ninja --version
    python3 -c 'import PIL; print("Pillow " + PIL.__version__)'
    committed=true
fi

printf '\nSummary%s\n' "$(if $dry_run; then printf ' (dry-run; no changes made)'; fi)"
echo "Packages already present (${#present[@]}): ${present[*]}"
if ((${#missing[@]})); then
    echo "Packages installed / planned (${#missing[@]}): ${missing[*]}"
fi
echo "PSPDEV: $pspdev_status; profile: $profile_status."
if [[ -f $prefix/VERSION_INSTALLED ]]; then
    printf 'VERSION_INSTALLED: '; cat -- "$prefix/VERSION_INSTALLED"; printf '\n'
fi
echo 'Git LFS: git lfs install is idempotent (planned only in dry-run).'
echo 'Open a new login shell, or run: source /etc/profile.d/pspdev.sh'
