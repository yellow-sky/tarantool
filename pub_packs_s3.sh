#!/bin/bash
set -e

# manual configuration
if [ "$BUCKET" == "" ]; then
    echo "ERROR: need to set BUCKET env variable to any like <1_10,2_2,2_3,2x>"
    exit 1
fi
branch=$BUCKET
update_dists=0

# configuration
if [ "$OS" == "" ]; then
    echo "ERROR: need to set OS env variable to any of <ubuntu,debian>"
    exit 1
fi
os=$OS
if [ "$DIST" == "" ]; then
    echo "ERROR: need to set DIST env variable to any like <bionic,cosmic,disco,trusty,xenial>"
    exit 1
fi
dists=$DIST
component=main
debdir=pool
aws='aws --endpoint-url https://hb.bizmrg.com'
s3="s3://tarantool_repo/$branch/$os"

# get packages from pointed location either default Docker's mirror
repo=$1
if [ "$repo" == "" ] ; then
    repo=/var/spool/apt-mirror/mirror/packagecloud.io/tarantool/$branch/$os
    # local workspace
    cd /mnt/tarantool_repo
    rm -rf dists pool
fi
debpath=$repo/$debdir
if [ ! -d $debpath ] ; then
    echo "Error: Current '$repo' has:"
    ls -al $repo
    echo "Usage: $0 [path to repository with '$debdir' directory in root path]"
    exit 1
fi

# create standalone repository with separate components
for dist in $DISTS ; do
    echo =================== DISTRIBUTION: $dist =========================
    updated_deb=0
    updated_dsc=0

    # 1(binaries). use reprepro tool to generate Packages file
    for deb in $debpath/$dist/$component/*/*/*.deb ; do
        [ -f $deb ] || continue
        locdeb=`echo $deb | sed "s#^$repo\/##g"`
        echo "DEB: $deb"
        # register DEB file to Packages file
        reprepro -Vb . includedeb $dist $deb
        # reprepro copied DEB file to local component which is not needed
        rm -rf $debdir/$component
        # to have all packages avoid reprepro set DEB file to its own registry
        rm -rf db
        # copy Packages file to avoid of removing by the new DEB version
        for packages in dists/$dist/$component/binary-*/Packages ; do
            if [ ! -f $packages.saved ] ; then
                # get the latest Packages file from S3
                $aws s3 ls "$s3/$packages" 2>/dev/null && \
                    $aws s3 cp --acl public-read \
                        "$s3/$packages" $packages.saved || \
                    touch $packages.saved
            fi
            # check if the DEB file already exists in Packages from S3
            if grep "^`grep "^SHA256: " $packages`$" $packages.saved ; then
                echo "WARNING: DEB file already registered in S3!"
                continue
            fi
            # store the new DEB entry
            cat $packages >>$packages.saved
            # save the registered DEB file to S3
            $aws s3 cp --acl public-read $deb $s3/$locdeb
            updated_deb=1
        done
    done

    # 1(sources). use reprepro tool to generate Sources file
    for dsc in $debpath/$dist/$component/*/*/*.dsc ; do
        [ -f $dsc ] || continue
        locdsc=`echo $dsc | sed "s#^$repo\/##g"`
        echo "DSC: $dsc"
        # register DSC file to Sources file
        reprepro -Vb . includedsc $dist $dsc
        # reprepro copied DSC file to component which is not needed
        rm -rf $debdir/$component
        # to have all sources avoid reprepro set DSC file to its own registry
        rm -rf db
        # copy Sources file to avoid of removing by the new DSC version
        sources=dists/$dist/$component/source/Sources
        if [ ! -f $sources.saved ] ; then
            # get the latest Sources file from S3
            $aws s3 ls "$s3/$sources" && \
                $aws s3 cp --acl public-read "$s3/$sources" $sources.saved || \
                touch $sources.saved
        fi
        # WORKAROUND: unknown why, but reprepro doesn`t save the Sources file
        gunzip -c $sources.gz >$sources
        # check if the DSC file already exists in Sources from S3
        hash=`grep '^Checksums-Sha256:' -A3 $sources | \
                tail -n 1 | awk '{print $1}'`
        if grep " $hash .*\.dsc$" $sources.saved ; then
            echo "WARNING: DSC file already registered in S3!"
            continue
        fi
        # store the new DSC entry
        cat $sources >>$sources.saved
        # save the registered DSC file to S3
        $aws s3 cp --acl public-read $dsc $s3/$locdsc
        tarxz=`echo $locdsc | sed 's#\.dsc$#.debian.tar.xz#g'`
        $aws s3 cp --acl public-read $repo/$tarxz "$s3/$tarxz"
        orig=`echo $locdsc | sed 's#-1\.dsc$#.orig.tar.xz#g'`
        $aws s3 cp --acl public-read $repo/$orig "$s3/$orig"
        updated_dsc=1
    done

    # check if any DEB/DSC files were newly registered
    [ "$update_dists" == "0" -a "$updated_deb" == "0" -a "$updated_dsc" == "0" ] && \
        continue || echo "Updating dists"

    # finalize the Packages file
    for packages in dists/$dist/$component/binary-*/Packages ; do
        mv $packages.saved $packages
    done

    # 2(binaries). update Packages file archives
    for packpath in dists/$dist/$component/binary-* ; do
        cd $packpath
        sed "s#Filename: $debdir/$component/#Filename: $debdir/$dist/$component/#g" -i Packages
        bzip2 -c Packages >Packages.bz2
        gzip -c Packages >Packages.gz
        cd -
    done

    # 2(sources). update Sources file archives
    cd dists/$dist/$component/source
    sed "s#Directory: $debdir/$component/#Directory: $debdir/$dist/$component/#g" -i Sources
    bzip2 -c Sources >Sources.bz2
    gzip -c Sources >Sources.gz
    cd -

    # 3. update checksums of the Packages* files in *Release files
    cd dists/$dist
    for file in `grep " $component/" Release | awk '{print $3}' | sort -u` ; do
        sz=`stat -c "%s" $file`
        md5=`md5sum $file | awk '{print $1}'`
        sha1=`sha1sum $file | awk '{print $1}'`
        sha256=`sha256sum $file | awk '{print $1}'`
        awk 'BEGIN{c = 0} ; {
                if ($3 == p) {
                    c = c + 1
                    if (c == 1) {print " " md  " " s " " p}
                    if (c == 2) {print " " sh1 " " s " " p}
                    if (c == 3) {print " " sh2 " " s " " p}
                } else {print $0}
            }' p="$file" s="$sz" md="$md5" sh1="$sha1" sh2="$sha256" \
            Release >Release.new
	mv Release.new Release
    done
    # resign the selfsigned InRelease file
    rm -rf InRelease
    gpg --clearsign -o InRelease Release
    # resign the Release file
    rm -rf Release.gpg
    gpg -abs -o Release.gpg Release
    cd -

    # 4. sync the latest distribution path changes to S3
    $aws s3 sync --acl public-read dists/$dist "$s3/dists/$dist"
done
