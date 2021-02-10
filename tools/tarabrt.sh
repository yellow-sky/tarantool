#!/bin/sh
set -eu

TOOL=$(basename "$0")
HELP=$(cat <<HELP
${TOOL} - Tarantool Automatic Bug Reporting Tool

This tool collects all required artefacts (listed below) and packs them into
a single archive with unified format:
  - /checklist - the plain text file with the list of tarball contents
  - /version   - the plain text file containing \`tarantool --version' output
  - /tarantool - the executable binary file produced the core dump
  - /coredump  - the core dump file produced by the executable
  - all shared libraries loaded (even via dlopen(3)) at the crash moment.

SYNOPSIS

  ${TOOL} [-h] [-c core] [-d dir] [-e executable] [-p procID] [-t datetime]

Supported options are:
  -c COREDUMP                   Use file COREDUMP as a core dump to examine.

  -d DIRECTORY                  Create the resulting archive with the artefacts
                                within DIRECTORY.

  -e TARANTOOL                  Use file TARANTOOL as the executable file for
                                examining with a core dump COREDUMP. If PID is
                                specified, the one from /proc/PID/exe is chosen
                                (see proc(5) for more info). If TARANTOOL is
                                omitted, /usr/bin/tarantool is chosen.

  -p PID                        PID of the dumped process, as seen in the PID
                                namespace in which the given process resides
                                (see %p in core(5) for more info). This flag
                                have to be set when ${TOOL} is used as
                                kernel.core_pattern pipeline script.

  -t DATETIME                   Time of dump, expressed as seconds since the
                                epoch, 1970-01-01 00:00:00 +0000 (UTC).

  -h                            Shows this message and exit.

USAGE

  - Manual usage. User can simply pack all necessary artefacts by running the
    following command.
    $ /path/to/${TOOL} -c ./core -d /tmp

  - Automatic usage. If user faces the failures often, one can set this script
    as a pipe reciever in kernel.core_pattern syntax.
    # sysctl -w kernel.core_pattern="|/absolute/path/to/${TOOL} -d /var/core -p %p -t %t"

HELP
)

# Parse CLI options.
OPTIONS=$(getopt -o c:d:e:hp:t: -n "${TOOL}" -- "$@")
eval set -- "${OPTIONS}"
while true; do
	case "$1" in
		--) shift; break;;
		-c) COREFILE=$2; shift 2;;
		-d) COREDIR=$2;  shift 2;;
		-e) BINARY=$2;   shift 2;;
		-p) PID=$2;      shift 2;;
		-t) TIME=$2;     shift 2;;
		-h) printf "%s\n", "${HELP}";
			exit 0;;
		*)  printf "Invalid option: $1\n%s\n", "${HELP}";
			exit 1;;
	esac
done

# Use the default values for the remaining parameters.
BINARY=${BINARY:-/usr/bin/tarantool}
COREDIR=${COREDIR:-${PWD}}
COREFILE=${COREFILE:-}
PID=${PID:-}
TIME=${TIME:-$(date +%s)}

# XXX: This section handles the case when the script is used for
# kernel.core_pattern. If PID is set and there is a directory in
# procfs with this PID, the script processes the core dumped by
# this process. If the process exe (or strictly saying its comm)
# is not 'tarantool' then the coredump is simply saved to the
# COREDIR; otherwise the dumped core is packed to the tarball.
if [ -n "${PID}" ] && [ -d /proc/"${PID}" ]; then
	BINARY=$(readlink /proc/"${PID}"/exe)
	CMDNAME=$(sed -z 's/\s$//' /proc/"${PID}"/comm)
	COREFILE=${COREDIR}/${CMDNAME}-core.${PID}.${TIME}
	cat >"${COREFILE}"
	if [ "${CMDNAME}" != 'tarantool' ]; then
		[ -t 1 ] && cat <<ALIENCOREDUMP
/proc/${PID}/comm doesn't equal to 'tarantool', so we assume the
obtained core is dumped by \`${CMDNAME}' and should be packed in
a different way. As a result it is simply stored to the file, so
you can process it on your own.

The file with core dump: ${COREFILE}
ALIENCOREDUMP
		exit 0;
	fi
fi

if [ -z "${COREFILE}" ]; then
	[ -t 1 ] && cat <<NOCOREDUMP
There is no core dump file passed to ${TOOL}. The artefacts can't
be collected. If you see this message, check the usage by running
\`${TOOL} -h': -c option is the obligatory one.
NOCOREDUMP
	exit 1;
fi

if file "${COREFILE}" | grep -qv 'core file'; then
	[ -t 1 ] && cat <<NOTACOREDUMP
Not a core dump: ${COREFILE}

The given COREDUMP file is not a valid core dump (see core(5) for
more info) or not even an ELF (see elf(5) for more info). If you
see this message, check the COREDUMP file the following way:
$ file ${COREFILE}
NOTACOREDUMP
	exit 1;
fi

# Check that gdb is installed.
if ! command -v gdb >/dev/null; then
	[ -t 1 ] && cat <<NOGDB
gdb is not installed, but it is obligatory for collecting the
loaded shared libraries from the core dump.

You can proceed collecting the artefacts manually later by running
the following command:
$ ${TOOL} -e ${BINARY} -c ${COREFILE}
NOGDB
	exit 1;
fi

if file "${BINARY}" | grep -qv 'executable'; then
	[ -t 1 ] && cat <<NOTELF
Not an ELF file: ${BINARY}

The given BINARY file is not an ELF (see elf(5) for more info).
If you see this message, check the BINARY file the following way:
$ file ${BINARY}
NOTELF
	exit 1;
fi

if gdb -batch -n "${BINARY}" -ex 'info symbol tarantool_version' 2>/dev/null | \
	grep -q 'tarantool_version in section .text'
then
	# XXX: This is a very ugly hack to implement 'unless'
	# operator in bash for a long pipeline as a conditional.
	:
else
	[ -t 1 ] && cat <<NOTARANTOOL
Not a Tarantool binary: ${BINARY}

The given BINARY file is not a Tarantool executable: there is no a
signature symbol in the binary file. If you see this message,
check the BINARY file the following way:
$ ${BINARY} --help
NOTARANTOOL
	exit 1;
fi

# Resolve the host name if possible.
HOSTNAME=$(hostname 2>/dev/null || echo hostname)

# Proceed with collecting and packing artefacts.
TMPDIR=$(mktemp -d -p "${COREDIR}")
TARLIST=${TMPDIR}/tarlist
VERSION=${TMPDIR}/version
ARCHIVENAME=${COREDIR}/tarantool-core-${PID:-N}-$(date +%Y%m%d%H%M -d @"${TIME}")-${HOSTNAME%%.*}.tar.gz

# Dump the version to checkout the right commit later.
${BINARY} --version >"${VERSION}"

# Collect the most important artefacts.
{
	echo "${BINARY}"
	echo "${COREFILE}"
	echo "${VERSION}"
} >>"${TARLIST}"

SEPARATOR1="Shared Object Library"
SEPARATOR2="Shared library is missing debugging information"
# XXX: This is kinda "postmortem ldd": the command below dumps the
# full list of the shared libraries the binary is linked against
# or those loaded via dlopen at the platform runtime.
# This is black voodoo magic. Do not touch. You are warned.
if gdb -batch -n "${BINARY}" -c "${COREFILE}" -ex 'info shared'    | \
	sed -n "/${SEPARATOR1}/,/${SEPARATOR2}/p;/${SEPARATOR2}/q" | \
	awk '{ print $NF }' | grep '^/' >>"${TARLIST}"
then
	# XXX: This is a very ugly hack to implement 'unless'
	# operator in bash for a long pipeline as a conditional.
	:
else
	[ -t 1 ] && cat <<COREMISMATCH
Core dump file is produced by the different Tarantool executable.

Looks like '${COREFILE}' is not generated by \`${BINARY}'.
If you see this message, please check that the given COREDUMP
is produced by the specified BINARY.
There are some temporary artefacts in ${TMPDIR}.
Remove it manually if you don't need them anymore.
COREMISMATCH
	exit 1;
fi

# Pack everything listed in TARLIST file into a tarball. To unify
# the archive format BINARY, COREFILE, VERSION and TARLIST are
# renamed while packing.
tar -czhf "${ARCHIVENAME}" -P -T "${TARLIST}" \
	--transform="s|${BINARY}|tarantool|"  \
	--transform="s|${COREFILE}|coredump|" \
	--transform="s|${TARLIST}|checklist|" \
	--transform="s|${VERSION}|version|"   \
	--add-file="${TARLIST}"

[ -t 1 ] && cat <<FINALIZE
The resulting is located here: ${ARCHIVENAME}

If you want to upload it, choose the available resourse
(e.g. http://transfer.sh) and run the following command:
$ curl -T ${ARCHIVENAME} <resourse-uri>
FINALIZE

# Cleanup temporary files.
[ -f "${TARLIST}" ] && rm -f "${TARLIST}"
[ -f "${VERSION}" ] && rm -f "${VERSION}"
[ -d "${TMPDIR}" ] && rmdir "${TMPDIR}"
