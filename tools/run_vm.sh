#!/bin/bash

set -e

usage() {
    echo "Usage: $0 -t=<freebsd_12[_*]|<OS_DIST>[_*]> [-a=<download,install,restore,start,login,snapshot,stop>] [-c=<command to run in VM>]"
}

NAME=
choose_vm() {
    os_dist=$1
    all_vms=`VBoxManage list vms | awk -F '"' '{print $2}' | sort | grep "^${os_dist}_[1-9]*$" || true`
    if [ "$all_vms" == "" ]; then
        echo "ERROR: the VM for the given os=$os and dist=$dist by pattern '^${os_dist}_[1-9]*\$' is not available"
        usage
        exit 1
    fi
    run_vms=`VBoxManage list runningvms | awk -F '"' '{print $2}' | sort | grep "^${os_dist}_[1-9]*$" || true`
    for vm in $all_vms ; do
        used=0
        for rvm in $run_vms ; do
            if [ "$vm" == "$rvm" ]; then
                used=1
                break
            fi
        done

        # let's setup the lock the file which will pause the
        # next job till the current job will start the VM:
        # lockfile will hold the current VM for 10 seconds
        # to start up the VM in this delay, while next job
        # will wait for 30 seconds for the lockfile released
        # to recheck the VM, otherwise it will try to find
        # the other free VM
        lfile=/tmp/$vm
        lockfile -1 -r 30 -l 10 $lfile
        if [ "$used" == "0" ] && ! VBoxManage list runningvms | awk -F '"' '{print $2}' | grep "^${vm}$" ; then
            NAME=$vm
            echo "Found free VM: $NAME"
            break
        fi
    done
    if [ "$NAME" == "" ]; then
        echo "WARNING: all of the appropriate VMs are in use!"
        echo "VMs: $all_vms"
    fi
}

SCP_LOCAL_SOURCES=0
for i in "$@"
do
case $i in
    -s|--scp_local_sources)
    SCP_LOCAL_SOURCES=1
    ;;
    -t=*|--template=*)
    TEMPLATE="${i#*=}"
    shift # past argument=value
    ;;
    -a=*|--actions=*)
    ACTIONS="${i#*=}"
    shift # past argument=value
    ;;
    -c=*|--command=*)
    COMMAND="${i#*=}"
    shift # past argument=value
    ;;
    -h|--help)
    usage
    exit 0
    ;;
    *)
    usage
    exit 1
    ;;
esac
done

if [[ $TEMPLATE =~ .*_.*_\* ]]; then
    while [ 1 ]; do
        choose_vm $TEMPLATE
        if [ "$NAME" != "" ]; then
            break
        fi
        echo "INFO: wait a minute to recheck for the free VM ..."
        sleep 60
    done
else
    NAME=$TEMPLATE
fi

if [ "$ACTIONS" == "" ]; then
    ACTIONS=restore,start,login,stop
    echo "WARNING: actions to start was not set: using default '$ACTIONS'"
    echo
fi

if [[ $ACTIONS =~ .*download.* ]]; then
    echo "Images drives and configuration can be downloaded from:"
    echo "    https://mcs.mail.ru/app/services/storage/bucket/objects/packages/?Prefix=testing%2Fvms%2F"

    # download the files
    for ftype in .ovf .mf -disk001.vmdk; do
        file=${NAME}$ftype
        wget -O - "https://download.tarantool.io/testing/vms/$file" >/tmp/$file
    done
fi

if [[ $ACTIONS =~ .*install.* ]]; then
    echo "Unregister the old VM with the same name"
    VBoxManage unregistervm $NAME --delete 2>/dev/null || true

    echo "Register the VM"
    VBoxManage import /tmp/${NAME}.ovf
fi

if [[ $ACTIONS =~ .*restore.* ]]; then
    echo "Restore the virtual machine snapshot"
    if ! VBoxManage snapshot $NAME restore $NAME ; then
        if ! [[ $ACTIONS =~ .*try_restore.* ]]; then
            exit 1
        fi
        echo "WARNING: the restore action failed, but it was expected - continuing"
    fi
fi

if [[ $ACTIONS =~ .*start.* ]]; then
    echo "Start the virtual machine without GUI"
    VBoxManage startvm $NAME --type headless
fi

exit_code=0
if [[ $ACTIONS =~ .*login.* ]]; then
    if [[ $NAME =~ freebsd_.* ]] ; then
        VMUSER=vagrant
    else
        VMUSER=tarantool
    fi

    host="${VMUSER}@127.0.0.1"
    ssh="ssh $host -p "`VBoxManage showvminfo $NAME | grep "host port = " \
	    | sed 's#.* host port = ##g' | sed 's#,.*##g'`

    echo "Try to ssh into the virtual machine"
    i=0
    while [[ $i -lt 1000 ]] || exit 1 ; do
        if $ssh "exit 0" ; then
            break
        fi
        echo "The host is not ready, let's wait a little to recheck ..."
        i=$(($i+1))
        sleep 10
    done
    if [ "$SCP_LOCAL_SOURCES" == "1" ]; then
        $ssh "cd ~ && rm -rf tarantool"
        tar czf - ../tarantool | $ssh tar xzf -
    fi
    if [ "$COMMAND" == "" ]; then
        $ssh || exit_code=$?
    else
        $ssh "$COMMAND" || exit_code=$?
    fi
    echo "The task exited with code: $exit_code"
fi

if [[ $ACTIONS =~ .*snapshot.* ]]; then
    echo "Take the virtual machine snapshot"
    VBoxManage snapshot $NAME delete $NAME 2>/dev/null || true
    VBoxManage snapshot $NAME take $NAME --pause
fi

if [[ $ACTIONS =~ .*stop.* ]]; then
    VBoxManage controlvm $NAME poweroff
fi

exit $exit_code
